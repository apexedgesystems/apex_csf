# HIL Flight Demo Results

**Executive:** ApexExecutive at 1000 Hz, HARD_PERIOD_COMPLETE mode
**HIL Link:** USART1 via FTDI FT232RL at 115200 baud (SLIP/CRC-16)
**Platform:** Raspberry Pi 4 (host) + NUCLEO-L476RG (flight controller)
**Duration:** 37-minute soak, 111K+ packets each direction, zero packet loss

---

## System Under Test

A closed-loop hardware-in-the-loop flight simulation. The Raspberry Pi runs
a 3DOF plant model (gravity + drag + point mass dynamics) under a full
ApexExecutive. The STM32 runs a PD flight controller under a LiteExecutive.
VehicleState flows from host to STM32 at 50 Hz; ControlCmd flows back at
50 Hz. A software-emulated flight controller runs in parallel for comparison.

```
Host (Pi 4, 1000 Hz)                          STM32 (100 Hz)
========================                       ========================
HilPlantModel    @ 100 Hz (dynamics)           FlightController @ 50 Hz
HilDriver #0     @  50 Hz (FTDI TX/RX)         USART1 SLIP framing
HilDriver #1     @  50 Hz (PTY emulated)       Heartbeat @ 1 Hz
VirtualFlightCtrl @  50 Hz (SW emulation)
HilComparator    @  10 Hz (diff real vs emu)
SystemMonitor    @   1 Hz (CPU, thermal)
```

---

## Results Summary (37-minute soak, FTDI link)

### STM32 Channel (Real Hardware)

| Metric                         | Value     | Significance                          |
| ------------------------------ | --------- | ------------------------------------- |
| Packets sent (host->STM32)     | 111,227   | Sustained 50 Hz for 37 minutes        |
| Packets received (STM32->host) | 114,449   | STM32 responds at full rate           |
| CRC errors                     | 0         | Zero corruption over 225K+ packets    |
| Sequence gaps                  | 0         | Zero lost packets in either direction |
| TX misses                      | 0         | Host never failed to send             |
| STM32 overhead                 | 6 us/tick | 0.03% of 10 ms tick budget            |
| STM32 cycle count              | 231,800   | Consistent 100 Hz executive           |

### Emulated Channel (Software, PTY)

| Metric           | Value   | Significance                     |
| ---------------- | ------- | -------------------------------- |
| Packets sent     | 111,228 | Matches real channel exactly     |
| Packets received | 111,204 | Near-perfect (24 startup misses) |
| CRC errors       | 0       | Software path has zero loss      |
| Sequence gaps    | 0       | Perfect sequencing               |

### Comparator (Real vs Emulated)

| Metric           | Value   | Significance                          |
| ---------------- | ------- | ------------------------------------- |
| Comparisons      | 22,250  | One comparison per 10 Hz tick         |
| Final divergence | 0.000 N | Real and emulated converged           |
| Mean divergence  | 0.202 N | Average across all samples            |
| Max divergence   | 200.0 N | Initial transient at startup          |
| Warnings         | 2,649   | Threshold breaches during convergence |

### Executive Health

| Metric             | Value      | Significance                       |
| ------------------ | ---------- | ---------------------------------- |
| Clock frequency    | 1000 Hz    | Rock-solid timing                  |
| Total cycles       | 2,231,800+ | 37 minutes of continuous operation |
| Frame overruns     | 119        | 0.005% overrun rate                |
| Watchdog warnings  | 0          | No timing anomalies                |
| Commands processed | 0          | Clean autonomous run               |

---

## Key Findings

### 1. Zero packet loss over the FTDI link

The FTDI FT232RL adapter on USART1 delivered 225,676 total packets (both
directions) with zero CRC errors and zero sequence gaps. This is a
fundamental improvement over the ST-Link VCP, which suffered ~9,000 sequence
gaps and ~1,000 CRC errors in a 5-minute test at the same data rate.

The sequence number fields added to VehicleState and ControlCmd (seqNum +
ackSeq) provide definitive proof of zero-loss operation. Each packet carries
an incrementing sequence number, and each side echoes the last received
sequence from the other side.

### 2. Real and emulated controllers converge to identical output

The HilComparator confirms that the STM32 flight controller and its software
emulation produce the same thrust commands. Final divergence is 0.000 N,
meaning the UART link introduces no error into the control loop. The initial
200 N divergence is expected (startup transient before the PD controller
stabilizes) and converges within the first 30 seconds.

### 3. Cross-platform code reuse is validated

The same FlightController class runs on both:

- STM32 Cortex-M4 at 80 MHz (bare-metal, LiteExecutive)
- Raspberry Pi Cortex-A72 at 1.8 GHz (Linux, VirtualFlightCtrl HW_MODEL)

Both produce identical control outputs given the same VehicleState input.
The SLIP framing and CRC-16 libraries are also shared across platforms.

### 4. Executive timing is production-quality

The 1000 Hz executive maintained timing with a 0.005% overrun rate (119 out
of 2.2M cycles). Overruns are brief (sub-millisecond) and do not affect the
control loop, which runs at 50 Hz (every 20th tick).

### 5. STM32 overhead is negligible

The STM32 flight controller completes its full tick (receive state, compute
control, send command) in 6 microseconds. This is 0.03% of the 10 ms tick
budget, leaving 99.97% headroom for more complex control algorithms.

---

## Historical Comparison: VCP vs FTDI

The sequence number feature was added specifically to diagnose packet loss
on the ST-Link VCP link. Prior soak tests showed ambiguous `rxMiss` counts
that could not distinguish between "no data available" polls and actual
lost packets.

| Metric           | ST-Link VCP (5 min)                  | FTDI FT232RL (37 min) |
| ---------------- | ------------------------------------ | --------------------- |
| Packets sent     | 10,327                               | 111,227               |
| Packets received | 1,665                                | 114,449               |
| Receive rate     | 16%                                  | 103%                  |
| CRC errors       | 943                                  | 0                     |
| Sequence gaps    | 8,967                                | 0                     |
| Root cause       | 64-byte USB endpoint buffer overflow | N/A                   |

The ST-Link V2 VCP uses a 64-byte USB HID endpoint that cannot sustain
bidirectional 50 Hz SLIP traffic. The FTDI FT232RL uses a dedicated USB
bulk endpoint with 4 KB hardware FIFO, eliminating the bottleneck entirely.

---

## Reproducing These Results

### Prerequisites

- Raspberry Pi 4 at `kalex@raspberrypi.local`
- NUCLEO-L476RG with FTDI adapter wired to USART1 (see HOW_TO_RUN.md)
- Docker and Docker Compose on dev machine

### Run

```bash
# Build + deploy (see HOW_TO_RUN.md for full procedure)
make release APP=ApexHilDemo

# Start on Pi (30-minute soak)
ssh kalex@raspberrypi.local 'cd ~/apex && \
  sudo rm -rf logs tlm system.log .apex_fs && \
  sudo ./run.sh ApexHilDemo --skip-cleanup --shutdown-after 1800 </dev/null &'

# Monitor
PYTHONPATH=tools/py/src python3 apps/apex_hil_demo/scripts/health.py \
  --host raspberrypi.local
```

### Key Checks

- `crcErr=0` in driver logs (zero corruption)
- `gaps=0` in driver logs (zero lost packets)
- `div=0.0000N` in comparator (real matches emulated)
- `freq=1000Hz` in health check (executive on time)

---

## Archived Runs

Stored as compressed tarballs in `docs/results/`:

```
apps/apex_hil_demo/docs/results/
  ftdi_soak_30min.tar.gz    Pi 4 + STM32, 37 min, FTDI link, zero packet loss
```

Extract with `tar xzf ftdi_soak_30min.tar.gz`. The archive contains:

- `system.log` -- Executive lifecycle and final statistics
- `logs/drivers/HilDriver_0.log` -- STM32 channel (tx/rx/seq/gaps per second)
- `logs/drivers/HilDriver_1.log` -- Emulated channel (PTY, reference)
- `logs/support/HilComparator_0.log` -- Real vs emulated divergence
- `logs/support/SystemMonitor_0.log` -- CPU and thermal telemetry
- `logs/models/*.log` -- Plant model and VirtualFlightCtrl logs
- `logs/core/*.log` -- Scheduler, registry, filesystem component logs
