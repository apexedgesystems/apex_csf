# How to Run: HIL Flight Demo

End-to-end procedure for building, deploying, and verifying ApexHilDemo on
Raspberry Pi 4 with STM32 NUCLEO-L476RG.

## Prerequisites

- Raspberry Pi 4 at `kalex@raspberrypi.local` with PREEMPT kernel
- STM32 NUCLEO-L476RG connected via USB-C (ST-Link) to Pi
- DSD TECH SH-U09C5 USB-to-TTL adapter (FTDI FT232RL) set to **3.3V**
- ST-Link tools installed on Pi (`sudo apt install stlink-tools`)
- Docker and Docker Compose on dev machine
- Network access from dev machine to Pi (port 9000 for C2)

### FTDI Wiring (HIL Data Link)

The HIL data channel uses USART1 via an FTDI adapter (same pinout as the
STM32 encryptor demo). The ST-Link VCP is not used for data -- its limited
USB endpoint buffer causes packet loss under sustained 50 Hz load.

| FTDI Pin | FTDI Label | Nucleo Pin | Nucleo Label | Header    |
| -------- | ---------- | ---------- | ------------ | --------- |
| 3        | TXD        | PA10 (RX)  | D2           | CN9 pin 3 |
| 4        | RXD        | PA9 (TX)   | D8           | CN5 pin 1 |
| 2        | GND        | GND        | GND          | CN6 pin 6 |

**Do not connect FTDI VCC.** The Nucleo is powered via its own USB-C cable.

Both the FTDI adapter USB and the Nucleo USB-C connect to the Pi's USB ports.

## Full Procedure (from distclean)

### 1. Build Native Debug

```bash
make distclean
make compose-debug
```

### 2. Run All Tests

```bash
make compose-testp
```

All tests must pass before proceeding.

### 3. Build Release Package

```bash
make release APP=ApexHilDemo
```

This builds RPi (aarch64) and STM32 (bare-metal) targets, resolves shared
library dependencies, creates the deployment package, and produces a tarball.

Output:

```
build/release/ApexHilDemo/
  rpi/
    run.sh                        # Launch script (supports direct + watchdog mode)
    bank_a/bin/ApexHilDemo        # Executive binary
    bank_a/bin/ApexWatchdog      # Watchdog supervisor binary
    bank_a/libs/*.so*             # Shared libraries
    bank_a/tprm/master.tprm      # TPRM configuration
  stm32/
    firmware/*.bin                # STM32 firmware binary
```

### 4. Clean Target on Pi

```bash
ssh kalex@raspberrypi.local 'sudo rm -rf ~/apex && mkdir ~/apex'
```

### 5. Deploy RPi Package

```bash
rsync -a build/release/ApexHilDemo/rpi/ kalex@raspberrypi.local:~/apex/
```

### 6. Copy TestPlugin_v1.so (for RELOAD_LIBRARY test)

```bash
scp build/rpi-aarch64-release/test_plugins/TestPlugin_v1.so \
    kalex@raspberrypi.local:~/apex/bank_a/libs/TestPlugin_0.so
```

The checkout script verifies library hot-swap by replacing TestPlugin_0.so with
TestPlugin_v2.so at runtime.

### 7. Copy STM32 Firmware to Pi

```bash
scp build/release/ApexHilDemo/stm32/firmware/*.bin \
    kalex@raspberrypi.local:~/apex/
```

### 8. Flash STM32 via Pi

```bash
ssh kalex@raspberrypi.local 'sudo st-flash write ~/apex/*.bin 0x08000000'
```

### 9. Reset STM32

```bash
ssh kalex@raspberrypi.local 'sudo st-flash reset'
```

### 10. Start ApexHilDemo

```bash
# Direct (no watchdog)
ssh kalex@raspberrypi.local 'cd ~/apex && \
  sudo rm -rf logs tlm rts ats db swap_history active_bank bank_b \
              system.log profile.log .apex_fs && \
  sudo ./run.sh ApexHilDemo --skip-cleanup </dev/null &'

# With watchdog (auto-restart on crash/hang)
ssh kalex@raspberrypi.local 'cd ~/apex && \
  sudo rm -rf logs tlm rts ats db swap_history active_bank bank_b \
              system.log profile.log .apex_fs && \
  sudo ./run.sh ApexWatchdog -- ApexHilDemo --skip-cleanup </dev/null &'
```

`run.sh` auto-adds `--config bank_a/tprm/master.tprm`, `--fs-root .`, and sets
`LD_LIBRARY_PATH`. In watchdog mode, args before `--` go to the watchdog, args
after `--` go to the executive.

**CRITICAL:** Always use `</dev/null` when starting headless. The executive's
stdin CLI reader interprets `p` as PAUSE and `q` as QUIT. Stale bytes from a
closed SSH pipe trigger unintended commands. Redirecting from `/dev/null` causes
immediate EOF which disables stdin polling entirely.

### 11. Wait for Startup

```bash
sleep 15
```

The system needs time to start, initialize all 11 components, open UART, and
begin communicating with the STM32. Cold-start STM32 (fresh flash) may take a
few seconds before it starts responding.

### 12. Run Checkout

```bash
PYTHONPATH=tools/py/src python3 apps/apex_hil_demo/scripts/checkout.py \
    --host raspberrypi.local
```

All 60 assertions across 16 tests must pass (0 failures).

### 13. Verify Post-Checkout Health (optional)

After checkout, the system should still be running (or restarted via test 15).
Verify:

```bash
PYTHONPATH=tools/py/src python3 -c "
from apex_tools.c2.client import AprotoClient
import struct
with AprotoClient('raspberrypi.local', 9000) as c2:
    r = c2.get_health()
    extra = r.get('extra', b'')
    if len(extra) >= 48:
        freq = struct.unpack_from('<H', extra, 32)[0]
        flags = extra[35]
        print(f'freq={freq}Hz paused={bool(flags & 0x02)} sleeping={bool(flags & 0x20)}')
"
```

### 14. Stop System

```bash
# Direct mode
ssh kalex@raspberrypi.local 'sudo kill $(pgrep ApexHilDemo)'

# Watchdog mode (signal the watchdog, which forwards SIGTERM to the executive)
ssh kalex@raspberrypi.local 'sudo kill $(pgrep ApexWatchdog)'
```

### 15. Review System Log

```bash
ssh kalex@raspberrypi.local 'sudo cat ~/apex/system.log'
```

Key checks:

- Frame overruns (should be low)
- Components registered (11 expected)
- No ERROR/FATAL lines
- STM32 driver: tx/rx counts, 0 CRC errors
- Comparator: divergence should converge to near zero

## Checkout Test Coverage

The checkout script (`scripts/checkout.py`) runs 16 tests with 60 assertions:

| #   | Test                 | Assertions | What it verifies                                                       |
| --- | -------------------- | ---------- | ---------------------------------------------------------------------- |
| 1   | Connectivity         | 1          | NOOP to executive succeeds                                             |
| 2   | Component addressing | 11         | NOOP to all 11 registered components                                   |
| 3   | Clock rate           | 1          | Execution frequency ~1000 Hz                                           |
| 4   | Executive health     | 4          | Health packet parsing, clock running, not paused, no watchdog warnings |
| 5   | STM32 driver stats   | 5          | INSPECT driver #0: tx/rx active, 0 CRC errors, 0 tx misses             |
| 6   | Comparator           | 3          | Divergence between real and emulated near zero                         |
| 7   | Sleep / Wake         | 5          | SLEEP command, FLAG_SLEEPING in health, WAKE, clock advancing          |
| 8   | Lock / Unlock        | 4          | Lock/unlock SystemMonitor, invalid UID returns COMPONENT_NOT_FOUND     |
| 9   | File transfer        | 4          | Chunked 8KB transfer with CRC-32C, progress callback, state IDLE       |
| 10  | File transfer abort  | 4          | Abort mid-transfer, state returns to IDLE, clean recovery              |
| 11  | TPRM reload          | 3          | Transfer TPRM file, RELOAD_TPRM command, system still running          |
| 12  | RELOAD_LIBRARY       | 4          | Hot-swap TestPlugin .so, NOOP before/after, clock advancing            |
| 13  | C2 latency           | 1          | 20-ping median < 50ms                                                  |
| 14  | Post-test health     | 1          | Clock rate still ~1000 Hz after all tests                              |
| 15  | RELOAD_EXECUTIVE     | 2          | Graceful restart via execve, connection drops                          |
| 16  | Post-restart verify  | 7          | Reconnect, low cycle count, clock rate, all components, not paused     |

### Skipping Tests

```bash
# Skip destructive tests (preserves running instance)
python3 checkout.py --host raspberrypi.local --skip-restart --skip-reload-lib

# Specify custom TestPlugin path
python3 checkout.py --host raspberrypi.local --plugin-so path/to/TestPlugin_v2.so
```

### Registered Components

| Name           | fullUid  | Type     |
| -------------- | -------- | -------- |
| Executive      | 0x000000 | Core     |
| Scheduler      | 0x000100 | Core     |
| Interface      | 0x000400 | Core     |
| Action         | 0x000500 | Core     |
| PlantModel     | 0x007800 | SW_MODEL |
| VFC            | 0x007900 | HW_MODEL |
| DriverReal     | 0x007A00 | DRIVER   |
| DriverEmulated | 0x007A01 | DRIVER   |
| Comparator     | 0x007B00 | SUPPORT  |
| SystemMonitor  | 0x00C800 | SUPPORT  |
| TestPlugin     | 0x00FA00 | SUPPORT  |

## Troubleshooting

### "Cannot connect" on checkout

System is not running or port 9000 is not reachable.

```bash
# Check if running
ssh kalex@raspberrypi.local 'pgrep -a ApexHilDemo'

# Check port
ssh kalex@raspberrypi.local 'ss -tlnp | grep 9000'
```

### STM32 tx/rx = 0

STM32 not communicating. Check USB connection and firmware:

```bash
# Check device exists
ssh kalex@raspberrypi.local 'ls -la /dev/ttyACM0'

# Reflash
ssh kalex@raspberrypi.local 'sudo st-flash write ~/apex/*.bin 0x08000000 && sudo st-flash reset'

# Wait and retry
sleep 10
```

### CRC errors after RELOAD_EXECUTIVE

The driver flushes the UART RX buffer on init to discard stale bytes from the
prior process. If CRC errors persist after restart, the flush may not have
caught all in-flight data. A brief wait (1-2 seconds) after restart before the
STM32 starts sending usually resolves this.

### System paused unexpectedly

If the system enters PAUSE state, the stdin CLI reader may have received garbage.
Always start with `</dev/null`. To recover:

```python
from apex_tools.c2.client import AprotoClient
with AprotoClient("raspberrypi.local", 9000) as c2:
    c2.send_command(0x000000, 0x0115)  # EXEC_CMD_RESUME
```

### Filesystem owned by root

Previous run created `.apex_fs` as root (sudo). Clean before re-running:

```bash
ssh kalex@raspberrypi.local 'cd ~/apex && sudo rm -rf .apex_fs'
```
