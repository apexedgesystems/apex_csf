#!/usr/bin/env python3
"""
ApexSpecDemo system checkout.

Proves the spec-driven component surface end to end: every command in
sensor/apex_data.toml is driven live against the running app, and every
effect is verified through INSPECT of the spec-generated data blocks.

Commands to model components ride the async queue (immediate ACK,
processed in the interface step), so effects -- not wire responses --
are the observable: TUNABLE_PARAM / STATE / OUTPUT reads bracket each
command. Response-payload readback over the wire is a framework-level
follow-on, not part of this demo.

Sections:
  1. Connectivity          NOOP to executive + all registered components
  2. Boot TPRM             Spec tunables loaded (driftRate, mode from TPRM)
  3. Model running         samples / output sequence advancing at 50 Hz
  4. SetMode               IDLE freezes sampling; MEASURE resumes
  5. Mode guard            FAULT_INJECT from IDLE rejected (rejects++);
                           from MEASURE accepted (output bias visible)
  6. Recalibrate           New reference published, drift/elapsed zeroed
  7. GetStats / Reset      GetStats accepted; Reset zeroes counters, mode
                           returns to the tunable default
  8. Malformed payload     Wrong-size SetMode never reaches user logic
                           (mode AND rejects both unchanged)
  9. Unknown opcode        Falls through to the tier base; app healthy
  10. Actuator boot        Proto-authored tunables live (rateLimit from TPRM)
  11. Actuator slew        Move commands a target; position ramps at the
                           rate limit and settles inside the hold band
  12. Halt / GetPosition   Halt freezes the target at the current position
  13. Actuator negatives   Out-of-range Move rejected (rejects++); wrong-size
                           Move never reaches user logic
  14. Post-test health     Clock rate still nominal

The actuator sections drive a component whose entire layout surface
was authored as protobuf (actuator/spec_actuator.proto) -- the same
generated-dispatch guarantees, from the other authoring format.

Usage:
  python3 checkout.py --host localhost
"""

import argparse
import struct
import sys
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.ops import protocol as proto  # noqa: E402
from apex_tools.ops.client import AprotoClient  # noqa: E402

PASS_COUNT = 0
FAIL_COUNT = 0

DIVIDER = "-" * 72

ALL_COMPONENTS = {
    "Executive": 0x000000,
    "Scheduler": 0x000100,
    "Interface": 0x000400,
    "SystemMonitor": 0x00C800,
    "SpecSensor": 0x00D400,
    "SpecActuator": 0x00D500,
}

SNS = 0x00D400
ACT = 0x00D500

# Spec command opcodes (sensor/apex_data.toml [[commands]])
CMD_SET_MODE = 0x0200
CMD_RECALIBRATE = 0x0201
CMD_GET_STATS = 0x0202
CMD_RESET = 0x0203

# Actuator command opcodes (actuator/apex_data.toml [[commands]];
# layouts from actuator/spec_actuator.proto)
CMD_MOVE = 0x0210
CMD_HALT = 0x0211
CMD_GET_POSITION = 0x0212

# Modes (SpecSensor::Mode)
IDLE, MEASURE, FAULT_INJECT = 0, 1, 2

# Time for a queued command to drain and a few 50 Hz steps to land.
SETTLE = 0.3


def check(name: str, condition: bool, detail: str = "") -> bool:
    global PASS_COUNT, FAIL_COUNT
    if condition:
        PASS_COUNT += 1
        print(f"  PASS  {name}")
    else:
        FAIL_COUNT += 1
        msg = f"  FAIL  {name}"
        if detail:
            msg += f"  ({detail})"
        print(msg)
    return condition


def section(title: str) -> None:
    print(f"\n{DIVIDER}")
    print(f"  {title}")
    print(DIVIDER)


def read_tunable(c2) -> dict:
    """SpecSensorTunableParams (20 bytes): rate, ref, drift, noise, mode."""
    r = c2.inspect(SNS, category=1)
    extra = r.get("extra", b"")
    if len(extra) < 20:
        return {}
    rate, ref, drift, noise = struct.unpack_from("<ffff", extra, 0)
    return {
        "sampleRateHz": rate,
        "referenceValue": ref,
        "driftRate": drift,
        "noiseAmplitude": noise,
        "mode": extra[16],
    }


def read_state(c2) -> dict:
    """SpecSensorState (20 bytes): elapsed, drift, samples, rejects, mode."""
    r = c2.inspect(SNS, category=2)
    extra = r.get("extra", b"")
    if len(extra) < 20:
        return {}
    elapsed, drift = struct.unpack_from("<ff", extra, 0)
    samples, rejects = struct.unpack_from("<II", extra, 8)
    return {
        "elapsed": elapsed,
        "drift": drift,
        "samples": samples,
        "rejects": rejects,
        "mode": extra[16],
    }


def read_output(c2) -> dict:
    """SpecSensorOutput (8 bytes): value, sequence."""
    r = c2.inspect(SNS, category=4)
    extra = r.get("extra", b"")
    if len(extra) < 8:
        return {}
    value = struct.unpack_from("<f", extra, 0)[0]
    sequence = struct.unpack_from("<I", extra, 4)[0]
    return {"value": value, "sequence": sequence}


def set_mode(c2, mode: int) -> dict:
    return c2.send_command(SNS, CMD_SET_MODE, bytes([mode]))


def read_act_tunable(c2) -> dict:
    """SpecActuatorTunableParams (12 bytes): rateLimit, holdBand, startPosition."""
    r = c2.inspect(ACT, category=1)
    extra = r.get("extra", b"")
    if len(extra) < 12:
        return {}
    rate, band, start = struct.unpack_from("<fff", extra, 0)
    return {"rateLimit": rate, "holdBand": band, "startPosition": start}


def read_act_state(c2) -> dict:
    """SpecActuatorState (16 bytes): position, target, moves, rejects."""
    r = c2.inspect(ACT, category=2)
    extra = r.get("extra", b"")
    if len(extra) < 16:
        return {}
    position, target = struct.unpack_from("<ff", extra, 0)
    moves, rejects = struct.unpack_from("<II", extra, 8)
    return {"position": position, "target": target, "moves": moves, "rejects": rejects}


def move(c2, position: float) -> dict:
    return c2.send_command(ACT, CMD_MOVE, struct.pack("<f", position))


def run_checkout(args: argparse.Namespace) -> int:
    global PASS_COUNT, FAIL_COUNT
    PASS_COUNT = 0
    FAIL_COUNT = 0

    print(f"\nApexSpecDemo Checkout: {args.host}:{args.port}")
    print("=" * 72)

    with AprotoClient(args.host, args.port, timeout=args.timeout) as c2:

        section("1. Connectivity")
        r = c2.noop()
        check("NOOP returns SUCCESS", r["status"] == 0, r["status_name"])
        for name, uid in ALL_COMPONENTS.items():
            r = c2.send_command(uid, proto.SYS_NOOP)
            check(f"{name} (0x{uid:06X})", r["status"] == 0, r["status_name"])

        section("2. Boot TPRM (spec tunables live)")
        p = read_tunable(c2)
        if check("TUNABLE_PARAM readable (20 bytes)", bool(p)):
            check(
                f"driftRate = {p['driftRate']:.2f} (TPRM value 0.5)",
                abs(p["driftRate"] - 0.5) < 1e-3,
            )
            check(
                f"referenceValue = {p['referenceValue']:.1f}",
                abs(p["referenceValue"] - 25.0) < 1e-3,
            )
            check(f"mode = {p['mode']} (TPRM boots MEASURE)", p["mode"] == MEASURE)
        base_ref = p.get("referenceValue", 25.0)

        section("3. Model Running (50 Hz step)")
        s0 = read_state(c2)
        o0 = read_output(c2)
        time.sleep(1.0)
        s1 = read_state(c2)
        o1 = read_output(c2)
        if check("STATE readable", bool(s0) and bool(s1)):
            dsamples = s1["samples"] - s0["samples"]
            check(f"samples advancing (~50/s, got {dsamples})", 35 <= dsamples <= 65)
        if check("OUTPUT readable", bool(o0) and bool(o1)):
            check(
                f"sequence advancing ({o0['sequence']} -> {o1['sequence']})",
                o1["sequence"] > o0["sequence"],
            )
            # value = ref + drift + noise; drift accumulates at 0.5/s
            check(
                f"value tracks reference ({o1['value']:.2f})",
                abs(o1["value"] - base_ref) < 0.5 * s1["elapsed"] + 1.0,
            )

        section("4. SetMode (IDLE freezes, MEASURE resumes)")
        r = set_mode(c2, IDLE)
        check("SetMode(IDLE) accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        s0 = read_state(c2)
        check(f"mode = IDLE ({s0.get('mode')})", s0.get("mode") == IDLE)
        time.sleep(0.5)
        s1 = read_state(c2)
        check(
            f"sampling frozen in IDLE (samples {s0.get('samples')} -> {s1.get('samples')})",
            s1.get("samples") == s0.get("samples"),
        )

        r = set_mode(c2, MEASURE)
        check("SetMode(MEASURE) accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        s2 = read_state(c2)
        check(f"mode = MEASURE ({s2.get('mode')})", s2.get("mode") == MEASURE)
        time.sleep(0.5)
        s3 = read_state(c2)
        check("sampling resumed", s3.get("samples", 0) > s2.get("samples", 0))

        section("5. Mode Guard (FAULT_INJECT only from MEASURE)")
        # Illegal: IDLE -> FAULT_INJECT (user hook rejects, counts it)
        set_mode(c2, IDLE)
        time.sleep(SETTLE)
        s0 = read_state(c2)
        set_mode(c2, FAULT_INJECT)
        time.sleep(SETTLE)
        s1 = read_state(c2)
        check(
            f"FAULT_INJECT from IDLE rejected (mode still {s1.get('mode')})", s1.get("mode") == IDLE
        )
        check(
            f"rejects incremented ({s0.get('rejects')} -> {s1.get('rejects')})",
            s1.get("rejects") == s0.get("rejects", 0) + 1,
        )

        # Legal: MEASURE -> FAULT_INJECT (output carries the bias)
        set_mode(c2, MEASURE)
        time.sleep(SETTLE)
        set_mode(c2, FAULT_INJECT)
        time.sleep(SETTLE)
        s2 = read_state(c2)
        o = read_output(c2)
        check(
            f"FAULT_INJECT from MEASURE accepted (mode {s2.get('mode')})",
            s2.get("mode") == FAULT_INJECT,
        )
        check(
            f"fault bias visible (value {o.get('value', 0.0):.1f})",
            o.get("value", 0.0) > base_ref + 25.0,
        )
        set_mode(c2, MEASURE)
        time.sleep(SETTLE)

        section("6. Recalibrate (new reference, drift zeroed)")
        s_pre = read_state(c2)
        payload = struct.pack("<f", 30.0)
        r = c2.send_command(SNS, CMD_RECALIBRATE, payload)
        check("Recalibrate(30.0) accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        p = read_tunable(c2)
        s = read_state(c2)
        check(
            f"referenceValue now {p.get('referenceValue', 0.0):.1f}",
            abs(p.get("referenceValue", 0.0) - 30.0) < 1e-3,
        )
        check(
            f"drift zeroed ({s_pre.get('drift', 0.0):.3f} -> {s.get('drift', 0.0):.3f})",
            s.get("drift", 1.0) < s_pre.get("drift", 0.0) or s.get("drift", 1.0) < 0.1,
        )
        time.sleep(0.4)
        o = read_output(c2)
        check(
            f"value tracks new reference ({o.get('value', 0.0):.2f})",
            abs(o.get("value", 0.0) - 30.0) < 2.0,
        )

        section("7. GetStats / Reset")
        r = c2.send_command(SNS, CMD_GET_STATS)
        check("GetStats accepted", r["status"] == 0, r["status_name"])

        r = c2.send_command(SNS, CMD_RESET)
        check("Reset accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        s = read_state(c2)
        o = read_output(c2)
        check(
            f"samples zeroed then re-advancing (got {s.get('samples')})",
            s.get("samples", 99999) < 100,
        )
        check(f"rejects zeroed (got {s.get('rejects')})", s.get("rejects") == 0)
        check(f"mode back to tunable default ({s.get('mode')})", s.get("mode") == MEASURE)
        check(
            f"output sequence restarted (got {o.get('sequence')})", o.get("sequence", 99999) < 100
        )

        section("8. Malformed Payload (never reaches user logic)")
        # SetModeRequest is 1 byte; send 2. The generated dispatch base
        # rejects on size before the hook runs: mode AND the user-side
        # rejects counter must both be untouched.
        s0 = read_state(c2)
        c2.send_command(SNS, CMD_SET_MODE, b"\x00\x00")
        time.sleep(SETTLE)
        s1 = read_state(c2)
        check(
            f"mode unchanged ({s0.get('mode')} -> {s1.get('mode')})",
            s1.get("mode") == s0.get("mode"),
        )
        check(
            f"rejects unchanged ({s0.get('rejects')} -> {s1.get('rejects')})",
            s1.get("rejects") == s0.get("rejects"),
        )

        section("9. Unknown Opcode (tier-base fallthrough)")
        s0 = read_state(c2)
        c2.send_command(SNS, 0x02FF)
        time.sleep(SETTLE)
        r = c2.send_command(SNS, proto.SYS_NOOP)
        check("SpecSensor NOOP after unknown opcode", r["status"] == 0, r["status_name"])
        s1 = read_state(c2)
        check("model still stepping", s1.get("samples", 0) > s0.get("samples", 0))
        check(
            f"state untouched (mode {s1.get('mode')}, rejects {s1.get('rejects')})",
            s1.get("mode") == s0.get("mode") and s1.get("rejects") == s0.get("rejects"),
        )

        section("10. Actuator Boot TPRM (proto-authored tunables live)")
        p = read_act_tunable(c2)
        if check("TUNABLE_PARAM readable (12 bytes)", bool(p)):
            check(
                f"rateLimit = {p['rateLimit']:.1f} (TPRM value 8.0)",
                abs(p["rateLimit"] - 8.0) < 1e-3,
            )
            check(
                f"holdBand = {p['holdBand']:.2f}",
                abs(p["holdBand"] - 0.1) < 1e-3,
            )
        rate = p.get("rateLimit", 8.0)

        section("11. Actuator Slew (Move -> ramp at rate limit -> settle)")
        s0 = read_act_state(c2)
        r = move(c2, 4.0)
        check("Move(4.0) accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        s1 = read_act_state(c2)
        check(
            f"target = 4.0 (got {s1.get('target', 0.0):.2f})",
            abs(s1.get("target", 0.0) - 4.0) < 1e-3,
        )
        check(
            f"moves incremented ({s0.get('moves')} -> {s1.get('moves')})",
            s1.get("moves") == s0.get("moves", 0) + 1,
        )
        check(
            f"slewing toward target (position {s1.get('position', 0.0):.2f})",
            s1.get("position", 0.0) > s0.get("position", 0.0),
        )
        # Ramp check: ~rate units/s while slewing.
        time.sleep(0.4)
        s2 = read_act_state(c2)
        dpos = s2.get("position", 0.0) - s1.get("position", 0.0)
        check(
            f"ramp ~{rate:.0f}/s (moved {dpos:.2f} in 0.4s)",
            0.2 * rate * 0.4 < dpos <= 1.3 * rate * 0.4 + 0.01,
        )
        # Settle: 4.0 units at 8/s = 0.5s from start; allow margin.
        time.sleep(0.6)
        s3 = read_act_state(c2)
        check(
            f"settled inside hold band (position {s3.get('position', 0.0):.3f})",
            abs(s3.get("position", 0.0) - 4.0) <= p.get("holdBand", 0.1) + 1e-3,
        )

        section("12. Halt / GetPosition")
        move(c2, -20.0)
        time.sleep(SETTLE)
        r = c2.send_command(ACT, CMD_HALT)
        check("Halt accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        s0 = read_act_state(c2)
        check(
            f"target frozen at position ({s0.get('target', 0.0):.2f} ~ "
            f"{s0.get('position', 0.0):.2f})",
            abs(s0.get("target", 0.0) - s0.get("position", 0.0)) <= p.get("holdBand", 0.1),
        )
        time.sleep(0.4)
        s1 = read_act_state(c2)
        check(
            f"position holding ({s0.get('position', 0.0):.2f} -> "
            f"{s1.get('position', 0.0):.2f})",
            abs(s1.get("position", 0.0) - s0.get("position", 0.0)) < 0.05,
        )
        r = c2.send_command(ACT, CMD_GET_POSITION)
        check("GetPosition accepted", r["status"] == 0, r["status_name"])

        section("13. Actuator Negatives")
        # Out-of-range target: user hook rejects and counts it.
        s0 = read_act_state(c2)
        move(c2, 2000.0)
        time.sleep(SETTLE)
        s1 = read_act_state(c2)
        check(
            f"out-of-range Move rejected (rejects {s0.get('rejects')} -> {s1.get('rejects')})",
            s1.get("rejects") == s0.get("rejects", 0) + 1,
        )
        check(
            f"target unchanged ({s1.get('target', 0.0):.2f})",
            abs(s1.get("target", 0.0) - s0.get("target", 0.0)) < 1e-3,
        )
        # Wrong-size payload: generated dispatch rejects before user code
        # (neither rejects nor target may change).
        s0 = read_act_state(c2)
        c2.send_command(ACT, CMD_MOVE, b"\x00\x00")
        time.sleep(SETTLE)
        s1 = read_act_state(c2)
        check("malformed Move: rejects unchanged", s1.get("rejects") == s0.get("rejects"))
        check(
            "malformed Move: target unchanged",
            abs(s1.get("target", 0.0) - s0.get("target", 0.0)) < 1e-3,
        )

        section("14. Post-Test Health")
        c1 = c2.get_clock_cycles()
        time.sleep(1.0)
        c2_val = c2.get_clock_cycles()
        rate = c2_val - c1
        check(f"Clock ~100 Hz (measured {rate})", 80 < rate < 120)

    print(f"\n{'=' * 72}")
    print(f"  Results: {PASS_COUNT} passed, {FAIL_COUNT} failed")
    print(f"{'=' * 72}")
    return 0 if FAIL_COUNT == 0 else 1


def main():
    parser = argparse.ArgumentParser(description="ApexSpecDemo system checkout")
    parser.add_argument("--host", default="localhost", help="Target hostname (default: localhost)")
    parser.add_argument("--port", type=int, default=9000, help="Target port (default: 9000)")
    parser.add_argument("--timeout", type=float, default=5.0, help="Command timeout (default: 5.0)")
    args = parser.parse_args()
    sys.exit(run_checkout(args))


if __name__ == "__main__":
    main()
