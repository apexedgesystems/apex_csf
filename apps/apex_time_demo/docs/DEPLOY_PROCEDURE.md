# Time Demo Deploy Procedure

`ApexTimeDemo` is a developer / SIL demo. It uses `MockPps` and a
synthetic GPS simulator, so there is no hardware deployment in the
sense that mission applications have one. This doc covers the two
deployment shapes that exist:

## 1. Local POSIX host (dev container, dev workstation, Jetson, RPi)

```bash
# Build
make compose-debug

# Run
./build/hosted-x86_64-debug/bin/ApexTimeDemo --shutdown-after 60
```

The executable is self-contained; no installer, no system service,
no `/dev/pps[N]` requirement. The `.apex_fs/` directory it creates
holds the standard ApexExecutive working tree (logs, TPRM banks,
sequence cache).

## 2. Release packaging (rpi)

The release pipeline packages `ApexTimeDemo` as a tar.gz like the
other POSIX demos. See `release.mk`:

```makefile
APP_REGISTRY += ApexTimeDemo
APP_ApexTimeDemo_PLATFORMS    := rpi
APP_ApexTimeDemo_rpi_TYPE     := posix
APP_ApexTimeDemo_rpi_BINARY   := ApexTimeDemo
```

The pipeline emits `output/ApexTimeDemo.tar.gz` containing the binary
and resolved shared libs. Untar on a target POSIX host:

```bash
tar xzf ApexTimeDemo.tar.gz
cd ApexTimeDemo
./bin/ApexTimeDemo
```

## What is intentionally NOT here

- **Real GPS integration**: the demo uses `MockPps`. Production apps
  wire `LinuxPps`/`Stm32Pps`/etc. per the
  [CUSTOMER_INTEGRATION.md](../../../src/system/core/components/time_server/CUSTOMER_INTEGRATION.md)
  guide.
- **TPRM authoring step**: the demo runs with TimeServer's default
  TPRM. Production deployments author a TPRM file and pass it via
  `--config`.
- **Firmware targets**: TimeServer is a hosted-POSIX core component;
  there is no `stm32` / `pico` / `esp32` / `c2000` / `arduino`
  variant of `ApexTimeDemo`. The corresponding HAL implementations
  (Stm32Pps etc.) are intended for production firmware projects.
