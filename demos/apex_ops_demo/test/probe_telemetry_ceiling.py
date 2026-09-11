#!/usr/bin/env python3
"""Telemetry-ceiling calibration probe (the blaster app's Q1 number).

Measures the sustained pushed-telemetry ceiling of one apex instance
on the target host: active channels x per-channel Hz through the
TelemetryManager subscription table. Each grid point boots the
vehicle fresh from its own pre-packed master (out-of-band via ssh),
the host measures PASSIVELY on the wire (no in-band commanding while
blasting -- command starvation under load is itself a finding, not a
harness hazard), and vehicle-side truth is read from the target's
logs after a clean stop:

  vehicle:  TM fail counter == 0 (push-queue overflow)
  RT:       heartbeat frame_overruns == 0
  wire:     client-received frames within 2% of expected in-window

Usage (host side; binary+libs already deployed to REMOTE_DIR):

  PYTHONPATH=tools/py/src python3 demos/apex_ops_demo/test/probe_telemetry_ceiling.py \\
      --ssh kalex@raspberrypi.local --host raspberrypi.local --seconds 30

Builds one master per grid point on the host (probe scheduler with
TM collect @ 100 Hz + the point's TM payload) and scps them up front.
"""

import argparse
import json
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, "tools/py/src")
from apex_tools.ops import protocol as proto  # noqa: E402
from apex_tools.ops.client import AprotoClient  # noqa: E402

TM_UID = 0x00C900
CHANNEL_OPCODE_BASE = 0x0340
TOOLS = "build/hosted-x86_64-debug/bin/tools/rust"
CROSS_PAYLOADS = "build/cross-rpi-release/demos/apex_ops_demo/exec/tprm/payloads"
SCHED_PROBE = "build/probe_scratch/scheduler_probe.tprm"
REMOTE_DIR = "/home/kalex/apex/blaster_probe"

# (fullUid, DataCategory OUTPUT=4) blocks the ops demo registers.
BLOCKS = [(0x00D000, 4), (0x00D001, 4), (0x00C800, 4)]

GRID = [(c, d) for c in (8, 16, 24, 32) for d in (4, 2, 1)]  # collect/4, /2, /1


def tm_toml(channels: int, rate_div: int, collect_hz: int) -> str:
    head = (
        f'collectRateHz = {{ type = "uint", size = 2, value = {collect_hz} }}\n'
        'reserved0 = { type = "uint", size = 2, value = 0 }\n'
        'reserved1 = { type = "uint", size = 4, value = 0 }\n'
    )
    subs = []
    for i in range(32):
        uid, cat = BLOCKS[i % len(BLOCKS)]
        active = 1 if i < channels else 0
        subs.append(
            "\n[[subscriptions]]\n"
            f'fullUid = {{ type = "uint", size = 4, value = {uid} }}\n'
            f'category = {{ type = "uint", size = 1, value = {cat} }}\n'
            f'active = {{ type = "uint", size = 1, value = {active} }}\n'
            f'opcode = {{ type = "uint", size = 2, value = {CHANNEL_OPCODE_BASE + i} }}\n'
            'offset = { type = "uint", size = 2, value = 0 }\n'
            'length = { type = "uint", size = 2, value = 0 }\n'
            f'rateDiv = {{ type = "uint", size = 2, value = {rate_div} }}\n'
            'reserved = { type = "uint", size = 2, value = 0 }\n'
        )
    return head + "".join(subs)


def sh(cmd: list, **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


def probe_infra(workdir: str, collect_hz: int) -> dict:
    """Executive + scheduler payloads for the requested collect rate."""
    paths = {}
    sched = open("build/probe_scratch/scheduler_probe.toml").read()
    sched = sched.replace(
        'freqN = { type = "uint", size = 2, value = 100 }',
        f'freqN = {{ type = "uint", size = 2, value = {collect_hz} }}',
    )
    sp = f"{workdir}/scheduler_{collect_hz}.toml"
    open(sp, "w").write(sched)
    paths["sched"] = f"{workdir}/scheduler_{collect_hz}.tprm"
    sh([f"{TOOLS}/cfg2bin", "--config", sp, "--output", paths["sched"], "--fulluid", "0x000100"])
    ex = open("demos/apex_ops_demo/tprm/toml/executive.toml").read()
    ex = ex.replace(
        'clockFrequencyHz = { type = "uint", size = 2, value = 100 }',
        f'clockFrequencyHz = {{ type = "uint", size = 2, value = {collect_hz} }}',
    )
    ep = f"{workdir}/executive_{collect_hz}.toml"
    open(ep, "w").write(ex)
    paths["exec"] = f"{workdir}/executive_{collect_hz}.tprm"
    sh([f"{TOOLS}/cfg2bin", "--config", ep, "--output", paths["exec"], "--fulluid", "0x000000"])
    return paths


def build_masters(workdir: str, collect_hz: int) -> list:
    infra = probe_infra(workdir, collect_hz)
    names = []
    for channels, rate_div in GRID:
        tag = f"{channels}c_{collect_hz // rate_div}hz"
        toml = f"{workdir}/tm_{tag}.toml"
        tm = f"{workdir}/tm_{tag}.tprm"
        master = f"{workdir}/master_{tag}.tprm"
        with open(toml, "w") as f:
            f.write(tm_toml(channels, rate_div, collect_hz))
        sh([f"{TOOLS}/cfg2bin", "--config", toml, "--output", tm, "--fulluid", f"0x{TM_UID:06X}"])
        args = [f"{TOOLS}/tprm_pack", "pack", "-o", master]
        for uid, fn in [
            (0x000400, "toml_interface_toml.tprm"),
            (0x000500, "toml_action_toml.tprm"),
            (0x00C800, "toml_system_monitor_toml.tprm"),
            (0x00D000, "toml_wave_gen_0_toml.tprm"),
            (0x00D001, "toml_wave_gen_1_toml.tprm"),
        ]:
            args += ["-e", f"0x{uid:06X}:{CROSS_PAYLOADS}/{fn}"]
        args += ["-e", f"0x000000:{infra['exec']}", "-e", f"0x000100:{infra['sched']}"]
        args += ["-e", f"0x{TM_UID:06X}:{tm}"]
        sh(args)
        names.append((channels, rate_div, f"master_{tag}.tprm"))
    return names


def remote(ssh_target: str, cmd: str) -> str:
    r = subprocess.run(["ssh", ssh_target, cmd], capture_output=True, text=True, timeout=60)
    return r.stdout.strip()


def drain_count(host: str, port: int, seconds: float) -> int:
    client = AprotoClient(host, port)
    for _attempt in range(10):
        try:
            client.connect()
            break
        except OSError:
            time.sleep(1.0)
    else:
        return 0
    sock = client._sock
    sock.settimeout(0.2)
    deadline = time.monotonic() + seconds
    count = 0
    buf = bytearray()
    while time.monotonic() < deadline:
        try:
            data = sock.recv(1 << 20)
        except TimeoutError:
            continue
        if not data:
            break
        buf.extend(data)
        for frame in proto.slip_decode_stream(buf):
            hdr = proto.parse_header(frame)
            if hdr and CHANNEL_OPCODE_BASE <= hdr["opcode"] < CHANNEL_OPCODE_BASE + 32:
                count += 1
    client.close()
    return count


def run_point(
    ssh_target: str,
    host: str,
    port: int,
    master: str,
    channels: int,
    rate_div: int,
    seconds: float,
    collect_hz: int = 100,
) -> dict:
    # sudo: hard-RT rates need FIFO scheduling; without CAP_SYS_NICE the
    # host's CFS jitter forges overruns the vehicle did not earn.
    remote(
        ssh_target,
        f"cd {REMOTE_DIR} && sudo pkill -x ApexOpsDemo; sleep 1; sudo rm -rf probe_fs; "
        f"sudo bash -c 'LD_LIBRARY_PATH={REMOTE_DIR}/libs nohup ./ApexOpsDemo "
        f"--config {master} --fs-root probe_fs --shutdown-after 600 --skip-cleanup "
        f"> boot.log 2>&1 < /dev/null &' ; sleep 7; pgrep -cx ApexOpsDemo",
    )
    pre = remote(ssh_target, f"tail -1 {REMOTE_DIR}/probe_fs/heartbeat.csv")
    pre_ovr = int(pre.split(",")[3]) if "," in pre and not pre.startswith("timestamp") else 0
    received = drain_count(host, port, seconds)
    tm_line = remote(
        ssh_target,
        f"sudo pkill -x ApexOpsDemo; sleep 2; "
        f"tail -1 {REMOTE_DIR}/probe_fs/logs/support/TelemetryManager_0.log; "
        f"tail -1 {REMOTE_DIR}/probe_fs/heartbeat.csv; "
        f'sudo grep -E "Telemetry frames" {REMOTE_DIR}/probe_fs/system.log | tail -1',
    )
    lines = tm_line.splitlines()
    sent = fail = overruns = -1
    wire_frames = -1
    for ln in lines:
        if "sent=" in ln:
            sent = int(ln.split("sent=")[1].split()[0])
            fail = int(ln.split("fail=")[1].split()[0])
        elif "Telemetry frames" in ln:
            wire_frames = int(ln.rsplit(":", 1)[1].strip())
        elif "," in ln and not ln.startswith("timestamp"):
            overruns = int(ln.split(",")[3]) - pre_ovr  # window delta
    per_channel_hz = float(collect_hz) / rate_div
    expected = channels * per_channel_hz * seconds
    return {
        "channels": channels,
        "hz": per_channel_hz,
        "sent_total": sent,
        "failures": fail,
        "overruns": overruns,
        "wire_frames": wire_frames,
        "received": received,
        "expected": expected,
        "recv_ratio": received / expected if expected else 0.0,
        "pass": fail == 0 and overruns == 0 and received / expected >= 0.98,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ssh", required=True, help="ssh target, e.g. user@pi")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--collect-hz", type=int, default=100)
    args = ap.parse_args()

    workdir = tempfile.mkdtemp(prefix="tm_probe_")
    masters = build_masters(workdir, args.collect_hz)
    subprocess.run(
        ["scp", "-q"] + [f"{workdir}/{m}" for _, _, m in masters] + [f"{args.ssh}:{REMOTE_DIR}/"],
        check=True,
    )
    print(f"{len(masters)} point-masters deployed; {args.seconds:.0f}s per point")

    results = []
    print("point            sent_tot  fail  ovr  recv_ratio  verdict")
    for channels, rate_div, master in masters:
        r = run_point(
            args.ssh,
            args.host,
            args.port,
            master,
            channels,
            rate_div,
            args.seconds,
            args.collect_hz,
        )
        results.append(r)
        print(
            f"{r['channels']:>2}ch @{r['hz']:>5.1f}Hz  {r['sent_total']:>8}  "
            f"{r['failures']:>4}  {r['overruns']:>3}  {r['recv_ratio']:>9.3f}  "
            f"{'PASS' if r['pass'] else 'SATURATED'}"
        )
    remote(args.ssh, "sudo pkill -x ApexOpsDemo; true")

    print(json.dumps(results, indent=1))
    best = max(
        (r for r in results if r["pass"]), key=lambda r: r["channels"] * r["hz"], default=None
    )
    if best:
        print(
            f"\nCeiling (per instance): {best['channels']} channels x "
            f"{best['hz']:.0f} Hz = {best['channels'] * best['hz']:.0f} frames/s sustained"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
