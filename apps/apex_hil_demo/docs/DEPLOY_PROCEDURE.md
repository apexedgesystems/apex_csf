# ApexHilDemo Deploy Procedure

End-to-end build, test, release, and deploy for Raspberry Pi with STM32.

## Prerequisites

- Pi: `kalex@raspberrypi.local` (Pi 4, PREEMPT kernel)
- STM32: NUCLEO-L476RG connected to Pi via ST-Link USB
- Docker Compose environment configured

## Procedure

```bash
# 1. Build native debug (from distclean)
make distclean
make compose-debug

# 2. Run all tests
make compose-testp

# 3. Build release (rpi + stm32, package, tarball)
make release APP=ApexHilDemo

# 4. Clean target on Pi
ssh kalex@raspberrypi.local 'sudo rm -rf ~/apex && mkdir ~/apex'

# 5. Deploy RPi package (bank_a/ + run.sh)
rsync -a build/release/ApexHilDemo/rpi/ kalex@raspberrypi.local:~/apex/

# 6. Copy TestPlugin_v1.so (for RELOAD_LIBRARY testing)
scp build/rpi-aarch64-release/test_plugins/TestPlugin_v1.so kalex@raspberrypi.local:~/apex/bank_a/libs/TestPlugin_0.so

# 7. Copy STM32 firmware to Pi
scp build/release/ApexHilDemo/stm32/firmware/*.bin kalex@raspberrypi.local:~/apex/

# 8. Flash STM32 via Pi (ST-Link connected to Pi)
ssh kalex@raspberrypi.local 'sudo st-flash write ~/apex/*.bin 0x08000000'

# 9. Reset STM32
ssh kalex@raspberrypi.local 'sudo st-flash reset'

# 10. Run ApexHilDemo
#     run.sh auto-adds --config bank_a/tprm/master.tprm and --fs-root .
#     CRITICAL: </dev/null prevents stdin CLI reader from getting garbage
#     when SSH disconnects (p=PAUSE, q=QUIT).

# Direct (no watchdog):
ssh kalex@raspberrypi.local 'cd ~/apex && \
  sudo rm -rf logs tlm rts ats db swap_history active_bank bank_b system.log profile.log .apex_fs && \
  sudo ./run.sh ApexHilDemo --skip-cleanup </dev/null &'

# With watchdog (auto-restart on crash/hang):
ssh kalex@raspberrypi.local 'cd ~/apex && \
  sudo rm -rf logs tlm rts ats db swap_history active_bank bank_b system.log profile.log .apex_fs && \
  sudo ./run.sh apex_watchdog -- ApexHilDemo --skip-cleanup </dev/null &'

# 11. Wait for system to start, then verify
sleep 10
PYTHONPATH=tools/py/src python3 apps/apex_hil_demo/scripts/checkout.py --host raspberrypi.local

# 12. Health check (safe for soak tests, read-only)
PYTHONPATH=tools/py/src python3 apps/apex_hil_demo/scripts/health.py --host raspberrypi.local

# 13. Stop via signal
ssh kalex@raspberrypi.local 'sudo kill $(pgrep ApexHilDemo)'      # direct mode
ssh kalex@raspberrypi.local 'sudo kill $(pgrep apex_watchdog)'    # watchdog mode

# 14. Verify logs
ssh kalex@raspberrypi.local 'sudo cat ~/apex/system.log'
```

## Key Notes

- **TPRM:** `shutdownMode = SIGNAL_ONLY (0)`, `shutdownAfterSeconds = 0`.
  No `--shutdown-after` needed. System runs until OS signal (SIGTERM/SIGINT).

- **Always use `</dev/null`** when starting headless. The executive's stdin
  CLI reader interprets 'p' as PAUSE, 'q' as QUIT. Stale bytes from a closed
  SSH pipe trigger unintended commands.

- **Previous .apex_fs owned by root:** Use `sudo rm -rf` to clean.

## Health Check Fields

| Field    | Meaning                                                           |
| -------- | ----------------------------------------------------------------- |
| tx / rx  | Frames sent / received                                            |
| crcErr   | CRC validation failures (should be 0)                             |
| seqGaps  | Sequence number gaps in ControlCmd (should be 0 = no lost frames) |
| txMiss   | sendState skipped (no UART or state source)                       |
| rxEmpty  | Non-blocking reads with no data available (normal)                |
| commLost | Comm watchdog detected link loss                                  |

**seqGaps is the definitive frame loss indicator.** rxEmpty counts expected
empty non-blocking reads and is not a problem.

## Filesystem After Deploy + Run

```
~/apex/
  run.sh                       # launch script
  bank_a/bin/ApexHilDemo       # executive binary
  bank_a/bin/apex_watchdog     # watchdog supervisor
  bank_a/libs/*.so*            # shared libraries
  bank_a/tprm/master.tprm     # TPRM config
  bank_b/{bin,libs,tprm}/     # inactive bank (created by doInit)
  active_bank                  # marker file
  system.log                   # system log
```
