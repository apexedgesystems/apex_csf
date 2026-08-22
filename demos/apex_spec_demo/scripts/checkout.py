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
  14. Bus driver           DRIVER tier: loopback tx==rx, oversize rejected
  15. Matrix               SUPPORT tier: all 15 vocabulary lanes intact,
                           independent checksum cross-proof
  16. Limits               Every constraint kind at its rail; in-band nudge
                           applies, out-of-band rejects
  17. ProtoMax             Maximal proto profile live, checksum cross-proof
  18. Channels             One spec, two instances, per-instance TPRM;
                           instance-isolated Zero
  19. Post-test health     Clock rate still nominal

The fleet spans the component taxonomy (SW_MODEL / DRIVER / SUPPORT),
both authoring formats, and the full spec vocabulary -- the demo is
the living compatibility suite for the spec-driven path.

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
    "SpecBusDriver": 0x00D600,
    "SpecMatrix": 0x00D700,
    "SpecLimits": 0x00D800,
    "SpecProtoMax": 0x00D900,
    "SpecChannel#0": 0x00DA00,
    "SpecChannel#1": 0x00DA01,
}

SNS = 0x00D400
ACT = 0x00D500
BUS = 0x00D600
MTX = 0x00D700
LIM = 0x00D800
PMX = 0x00D900
CHA = 0x00DA00
CHB = 0x00DA01

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

# Fleet command opcodes (per-component apex_data.toml [[commands]])
CMD_SEND_FRAME = 0x0220
CMD_FLUSH = 0x0221
CMD_BUS_STATS = 0x0222
CMD_SNAPSHOT = 0x0230
CMD_NUDGE = 0x0238
CMD_REPORT = 0x0241
CMD_ZERO = 0x0248


def fold_checksum(data: bytes) -> int:
    """Mirror of the components' XOR fold (byte << (i%4)*8)."""
    sum_ = 0
    for i, b in enumerate(data):
        sum_ ^= b << ((i % 4) * 8)
    return sum_


def cstr(buf: bytes) -> str:
    return buf.split(b"\x00", 1)[0].decode("ascii", "replace")


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
    """SpecActuatorTunableParams (20 bytes): rateLimit, holdBand, startPosition, axisLabel."""
    r = c2.inspect(ACT, category=1)
    extra = r.get("extra", b"")
    if len(extra) < 20:
        return {}
    rate, band, start = struct.unpack_from("<fff", extra, 0)
    label = extra[12:20].split(b"\x00", 1)[0].decode("ascii", "replace")
    return {"rateLimit": rate, "holdBand": band, "startPosition": start, "axisLabel": label}


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
        if check("TUNABLE_PARAM readable (20 bytes)", bool(p)):
            check(
                f"rateLimit = {p['rateLimit']:.1f} (TPRM value 8.0)",
                abs(p["rateLimit"] - 8.0) < 1e-3,
            )
            check(
                f"holdBand = {p['holdBand']:.2f}",
                abs(p["holdBand"] - 0.1) < 1e-3,
            )
            # Bounded string: authored "X-AXIS" in an 8-byte null-padded
            # char buffer (proto (apex.capacity) = 8).
            check(
                f"axisLabel = '{p.get('axisLabel')}' (bounded string live)",
                p.get("axisLabel") == "X-AXIS",
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

        section("14. Bus Driver (DRIVER tier: loopback round-trips)")
        r = c2.inspect(BUS, category=1)
        extra = r.get("extra", b"")
        if check("bus TUNABLE readable (4 bytes)", len(extra) >= 4):
            delay, maxlen = struct.unpack_from("<HB", extra, 0)
            check(f"loopDelayTicks = {delay} (TPRM value 2)", delay == 2)
            check(f"maxFrameBytes = {maxlen} (TPRM value 12)", maxlen == 12)
        frame = bytes([0xA5] * 8)
        r = c2.send_command(BUS, CMD_SEND_FRAME, bytes([8, 0, 0, 0]) + frame + bytes(8))
        check("SendFrame(8 bytes) accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        r = c2.inspect(BUS, category=2)
        extra = r.get("extra", b"")
        if check("bus STATE readable (24 bytes)", len(extra) >= 24):
            tx, rx, txb, rxb, rej = struct.unpack_from("<IIIII", extra, 0)
            check(f"loopback round-trip (tx={tx} rx={rx})", tx == 1 and rx == 1)
            check(f"byte counters match (txB={txb} rxB={rxb})", txb == 8 and rxb == 8)
            # Oversize frame: user hook rejects (13 > maxFrameBytes 12).
            c2.send_command(BUS, CMD_SEND_FRAME, bytes([13, 0, 0, 0]) + bytes(16))
            time.sleep(SETTLE)
            r = c2.inspect(BUS, category=2)
            rej2 = struct.unpack_from("<I", r.get("extra", b""), 16)[0]
            check(f"oversize frame rejected (rejects {rej} -> {rej2})", rej2 == rej + 1)
        r = c2.send_command(BUS, CMD_FLUSH)
        check("Flush accepted", r["status"] == 0, r["status_name"])
        r = c2.send_command(BUS, CMD_BUS_STATS)
        check("GetStats accepted", r["status"] == 0, r["status_name"])

        section("15. Matrix (SUPPORT tier: full type vocabulary live)")
        r = c2.inspect(MTX, category=1)
        extra = r.get("extra", b"")
        if check("matrix TUNABLE readable (80 bytes)", len(extra) >= 80):
            i_wide, u_wide = struct.unpack_from("<qQ", extra, 0)
            precise = struct.unpack_from("<d", extra, 16)[0]
            i_word, u_word = struct.unpack_from("<iI", extra, 24)
            ratio = struct.unpack_from("<f", extra, 32)[0]
            gains = struct.unpack_from("<4H", extra, 36)
            i_short, u_short = struct.unpack_from("<hH", extra, 44)
            i_tiny = struct.unpack_from("<b", extra, 48)[0]
            u_tiny, enabled = extra[49], extra[50]
            label = cstr(extra[51:63])
            tags = (cstr(extra[63:69]), cstr(extra[69:75]))
            blob = tuple(extra[75:80])
            check(
                f"int64/uint64 lanes ({i_wide}, {u_wide})",
                i_wide == -8000000001 and u_wide == 9000000001,
            )
            check(f"double lane ({precise})", abs(precise - 2.5) < 1e-9)
            check(f"int32/uint32 lanes ({i_word}, {u_word})", i_word == -70001 and u_word == 70001)
            check(f"float lane ({ratio})", abs(ratio - 0.75) < 1e-6)
            check(f"uint16 array lane {gains}", gains == (10, 20, 30, 40))
            check(f"int16/uint16 lanes ({i_short}, {u_short})", i_short == -301 and u_short == 301)
            check(
                f"int8/uint8/bool lanes ({i_tiny}, {u_tiny}, {enabled})",
                i_tiny == -6 and u_tiny == 6 and enabled == 1,
            )
            check(f"bounded string lane '{label}'", label == "FULL-MATRIX")
            check(f"string array lane {tags}", tags == ("ALPHA", "BETA"))
            check(f"byte buffer lane {blob}", blob == (1, 2, 3, 4, 5))
            # Cross-proof: the component's running checksum over these
            # same bytes must match our independent fold.
            r = c2.inspect(MTX, category=2)
            sextra = r.get("extra", b"")
            if check("matrix STATE readable", len(sextra) >= 8):
                steps, checksum = struct.unpack_from("<II", sextra, 0)
                check(
                    f"checksum cross-proof (0x{checksum:08X})",
                    checksum == fold_checksum(extra[:80]) and steps > 0,
                )
        r = c2.send_command(MTX, CMD_SNAPSHOT)
        check("GetSnapshot accepted (SUPPORT dispatch)", r["status"] == 0, r["status_name"])

        section("15b. Spec TPRM Reload (RELOAD applies, never just verifies)")
        # The defect class this pins: a reload that verifies the stamped
        # file, logs RELOAD_TPRM_OK, and silently never applies (isolated
        # by the zenith session, 2026-08-22). The real pipeline end to
        # end: edit the value TOML -> cfg2bin stamps the v3 prelude ->
        # upload + RELOAD -> the ACTIVE bytes must change.
        import subprocess

        repo = __import__("pathlib").Path(__file__).resolve().parents[3]
        cfg2bin = repo / "build/hosted-x86_64-debug/bin/tools/rust/cfg2bin"
        src_toml = repo / "demos/apex_spec_demo/tprm/toml/spec_matrix.toml"
        tprm_dir = repo / "build/hosted-x86_64-debug/demos/apex_spec_demo/exec/tprm"
        restore_tprm = tprm_dir / "payloads/toml_spec_matrix_toml.tprm"
        if cfg2bin.is_file() and src_toml.is_file() and restore_tprm.is_file():
            r = c2.inspect(MTX, category=1)
            before = r.get("extra", b"")[:80]
            ratio0 = struct.unpack_from("<f", before, 32)[0]
            edited = src_toml.read_text().replace("value = 0.75", "value = 2.5")
            tmp = __import__("tempfile").mkdtemp()
            toml_path = __import__("pathlib").Path(tmp) / "spec_matrix.toml"
            toml_path.write_text(edited)
            out_path = __import__("pathlib").Path(tmp) / "00d700.tprm"
            gen = subprocess.run(
                [str(cfg2bin), "-c", str(toml_path), "-o", str(out_path), "--fulluid", "0x00D700"],
                capture_output=True,
            )
            check("cfg2bin stamps the edited set", gen.returncode == 0, gen.stderr.decode()[:80])
            r = c2.update_tprm(MTX, str(out_path))
            check("RELOAD_TPRM returns SUCCESS", r["status"] == 0, r["status_name"])
            time.sleep(1.2)  # a 1 Hz step must land to refresh the checksum
            r = c2.inspect(MTX, category=1)
            after = r.get("extra", b"")[:80]
            ratio1 = struct.unpack_from("<f", after, 32)[0]
            check(f"ratio APPLIED ({ratio0} -> {ratio1})", abs(ratio1 - 2.5) < 1e-6)
            r = c2.inspect(MTX, category=2)
            sextra = r.get("extra", b"")
            checksum = struct.unpack_from("<I", sextra, 4)[0]
            check(
                f"active bytes changed (checksum 0x{checksum:08X} matches new set)",
                checksum == fold_checksum(after) and after != before,
            )
            # Restore the authored set so later sections see boot values.
            r = c2.update_tprm(MTX, str(restore_tprm))
            check("restore authored set", r["status"] == 0, r["status_name"])
            time.sleep(1.2)
            r = c2.inspect(MTX, category=1)
            ratio2 = struct.unpack_from("<f", r.get("extra", b""), 32)[0]
            check(f"restored ({ratio2})", abs(ratio2 - 0.75) < 1e-6)
        else:
            check(
                "reload prerequisites present",
                False,
                "cfg2bin/toml/payload missing from build tree",
            )

        section("16. Limits (every constraint kind at its rail)")
        r = c2.inspect(LIM, category=1)
        extra = r.get("extra", b"")
        if check("limits TUNABLE readable (36 bytes)", len(extra) >= 36):
            floor_v, ceil_v, banded, stepped = struct.unpack_from("<ffff", extra, 0)
            table = struct.unpack_from("<4f", extra, 16)
            mode = extra[32]
            check(f"min-only at edge ({floor_v})", floor_v == 0.0)
            check(f"max-only at edge ({ceil_v})", ceil_v == 100.0)
            check(f"band at max edge ({banded})", banded == 10.0)
            check(f"step-rail value ({stepped})", abs(stepped - 2.25) < 1e-6)
            check(
                f"array band edges {tuple(round(x, 2) for x in table)}",
                abs(table[0] + 1.0) < 1e-6 and abs(table[3] - 1.0) < 1e-6,
            )
            check(f"allowed-list value ({mode})", mode == 7)
        c2.send_command(LIM, CMD_NUDGE, struct.pack("<f", 3.0))
        time.sleep(SETTLE)
        c2.send_command(LIM, CMD_NUDGE, struct.pack("<f", 8.0))
        time.sleep(SETTLE)
        r = c2.inspect(LIM, category=2)
        extra = r.get("extra", b"")
        if check("limits STATE readable", len(extra) >= 12):
            value = struct.unpack_from("<f", extra, 0)[0]
            nudges, rejects = struct.unpack_from("<II", extra, 4)
            check(f"in-band nudge applied (value={value})", abs(value - 3.0) < 1e-6)
            check(
                f"out-of-band nudge rejected (nudges={nudges}, rejects={rejects})",
                nudges == 1 and rejects == 1,
            )

        section("17. ProtoMax (maximal proto profile live)")
        r = c2.inspect(PMX, category=1)
        extra = r.get("extra", b"")
        if check("protoMax TUNABLE readable (80 bytes)", len(extra) >= 80):
            precise = struct.unpack_from("<d", extra, 0)[0]
            u_wide, i_wide = struct.unpack_from("<Qq", extra, 8)
            taps = struct.unpack_from("<3f", extra, 36)
            tag = cstr(extra[52:60])
            aliases = (cstr(extra[60:64]), cstr(extra[64:68]))
            cookie = tuple(extra[68:72])
            check(
                f"double/uint64/int64 lanes ({precise}, {u_wide}, {i_wide})",
                abs(precise - 3.5) < 1e-9 and u_wide == 9000000002 and i_wide == -8000000002,
            )
            check(
                f"bounded float array {tuple(round(t, 2) for t in taps)}",
                abs(taps[0] - 0.1) < 1e-6 and abs(taps[2] - 0.3) < 1e-6,
            )
            check(f"bounded string '{tag}'", tag == "PMX-OK")
            check(f"string array {aliases}", aliases == ("AA", "BB"))
            check(
                f"byte buffer {tuple(hex(c) for c in cookie)}", cookie == (0xDE, 0xAD, 0xBE, 0xEF)
            )
            r = c2.inspect(PMX, category=2)
            sextra = r.get("extra", b"")
            if check("protoMax STATE readable", len(sextra) >= 8):
                steps, checksum = struct.unpack_from("<II", sextra, 0)
                check(
                    f"checksum cross-proof (0x{checksum:08X})",
                    checksum == fold_checksum(extra[:80]) and steps > 0,
                )
        r = c2.send_command(PMX, CMD_REPORT)
        check("Report accepted", r["status"] == 0, r["status_name"])

        section("18. Channels (one spec, two instances, two configs)")
        cfg = {}
        for name, uid in (("A", CHA), ("B", CHB)):
            r = c2.inspect(uid, category=1)
            extra = r.get("extra", b"")
            if check(f"channel {name} TUNABLE readable", len(extra) >= 12):
                gain, offset = struct.unpack_from("<ff", extra, 0)
                cfg[name] = (gain, offset, cstr(extra[8:12]))
        if len(cfg) == 2:
            check(f"instance A config {cfg['A']}", cfg["A"] == (2.0, 1.0, "CH-A"))
            check(f"instance B config {cfg['B']}", cfg["B"] == (-1.0, 5.0, "CH-B"))
        va0 = struct.unpack_from("<f", c2.inspect(CHA, category=4).get("extra", b"\0" * 8), 0)[0]
        vb0 = struct.unpack_from("<f", c2.inspect(CHB, category=4).get("extra", b"\0" * 8), 0)[0]
        time.sleep(0.6)
        va1 = struct.unpack_from("<f", c2.inspect(CHA, category=4).get("extra", b"\0" * 8), 0)[0]
        vb1 = struct.unpack_from("<f", c2.inspect(CHB, category=4).get("extra", b"\0" * 8), 0)[0]
        check(f"A ramps up ({va0:.2f} -> {va1:.2f})", va1 > va0)
        check(f"B ramps down ({vb0:.2f} -> {vb1:.2f})", vb1 < vb0)
        r = c2.send_command(CHA, CMD_ZERO)
        check("Zero(A) accepted", r["status"] == 0, r["status_name"])
        time.sleep(SETTLE)
        va2 = struct.unpack_from("<f", c2.inspect(CHA, category=4).get("extra", b"\0" * 8), 0)[0]
        vb2 = struct.unpack_from("<f", c2.inspect(CHB, category=4).get("extra", b"\0" * 8), 0)[0]
        check(f"A restarted near offset ({va2:.2f})", va2 < va1)
        check(f"B unaffected by A's Zero ({vb2:.2f})", vb2 < vb1)

        section("19. Post-Test Health")
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
