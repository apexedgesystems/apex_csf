# Apex Docker Infrastructure

Containerized build and development environments for the Apex CSF C++ project.
Provides consistent toolchains across native builds, cross-compilation targets,
and embedded microcontroller platforms.

## Quick Start

```bash
# Build and enter CPU development shell
make shell-dev

# Build and enter CUDA development shell
make shell-dev-cuda

# Build for Jetson (aarch64 + CUDA)
make shell-dev-jetson
make jetson-release

# Build all images
make docker-all
```

## Image Hierarchy

```
apex.base (Ubuntu 24.04, Clang 20-21, CMake 4.x, Rust, ccache, mold)
+-- apex.dev.cpu ---------------------+-- apex.builder.cpu
|   +-- apex.dev.stm32               |
|   +-- apex.dev.esp32               |
|   +-- apex.dev.pico                |
|   +-- apex.dev.zephyr              |
|   +-- apex.dev.arduino             |
|   +-- apex.dev.atmega328pb         |
|   +-- apex.dev.pic32               |
|   +-- apex.dev.c2000               |
|   +-- apex.dev.rpi ----------------+-- apex.builder.rpi
|   \-- apex.dev.riscv64 ------------+-- apex.builder.riscv64
|                                    |
\-- apex.dev.cuda -------------------+-- apex.builder.cuda
    \-- apex.dev.jetson -------------+-- apex.builder.jetson

apex.final (busybox, collects tarballs from builders)
```

Toolchain setup is embedded directly in each dev image (no separate toolchain
layer). Dev images extend `apex.base` (CPU-derived) or `apex.dev.cuda`
(GPU-derived) and install their platform-specific compilers and SDKs inline.

## Images

### Base Image

| Image       | Base         | Description                                                  |
| ----------- | ------------ | ------------------------------------------------------------ |
| `apex.base` | Ubuntu 24.04 | Clang 20-21, CMake 4.x, Ninja, ccache, mold, Rust, profilers |

Includes: LLVM toolchain, formatters (clang-format, black, shfmt, hadolint),
math/crypto libraries (OpenBLAS, LAPACK, SuiteSparse, OpenSSL), Doxygen, UPX,
FlameGraph, google-perftools, bpftrace.

### Development Shells

| Image                  | Prompt          | Target          | Use Case                  |
| ---------------------- | --------------- | --------------- | ------------------------- |
| `apex.dev.cpu`         | Blue [CPU]      | x86_64 Linux    | Native development        |
| `apex.dev.cuda`        | Green [CUDA]    | x86_64 + CUDA   | GPU development           |
| `apex.dev.jetson`      | Cyan [JETSON]   | Jetson          | NVIDIA Jetson cross-build |
| `apex.dev.rpi`         | Magenta [RPI]   | Raspberry Pi    | ARM64 Linux cross-build   |
| `apex.dev.riscv64`     | White [RISCV]   | RISC-V 64       | RISC-V Linux cross-build  |
| `apex.dev.zephyr`      | Red [ZEPHYR]    | Multi-arch RTOS | Zephyr RTOS development   |
| `apex.dev.stm32`       | Yellow [STM32]  | ARM Cortex-M    | STM32 microcontrollers    |
| `apex.dev.esp32`       | Magenta [ESP32] | Xtensa/RISC-V   | ESP32 microcontrollers    |
| `apex.dev.pico`        | White [PICO]    | ARM Cortex-M0+  | Raspberry Pi Pico         |
| `apex.dev.arduino`     | Red [ARDUINO]   | AVR             | Arduino boards            |
| `apex.dev.atmega328pb` | Red [AVR]       | AVR             | Bare-metal ATmega328PB    |
| `apex.dev.pic32`       | Cyan [PIC32]    | MIPS            | PIC32 microcontrollers    |
| `apex.dev.c2000`       | Yellow [C2000]  | TI DSP          | TI C2000 DSPs             |

### Builder Images

| Image                  | Output                          | Description              |
| ---------------------- | ------------------------------- | ------------------------ |
| `apex.builder.cpu`     | `build/native-linux-release/`   | x86_64 release artifacts |
| `apex.builder.cuda`    | `build/native-linux-release/`   | x86_64 + CUDA artifacts  |
| `apex.builder.jetson`  | `build/jetson-aarch64-release/` | Jetson release artifacts |
| `apex.builder.rpi`     | `build/rpi-aarch64-release/`    | Raspberry Pi artifacts   |
| `apex.builder.riscv64` | `build/riscv64-linux-release/`  | RISC-V 64 artifacts      |
| `apex.builder.stm32`   | `build/stm32/firmware/`         | STM32 firmware artifacts |

### Final Image

| Image        | Description                               |
| ------------ | ----------------------------------------- |
| `apex.final` | Lightweight busybox with release tarballs |

Collects artifacts from all builder images and creates gzipped tarballs:
`cpu-release.tar.gz`, `cuda-release.tar.gz`, `jetson-release.tar.gz`,
`stm32-firmware.tar.gz`.

## Make Targets

### Building Images

```bash
# All images
make docker-all

# Image groups
make docker-devs          # All dev shell images
make docker-builders      # All builder images

# Individual images
make docker-base
make docker-dev
make docker-dev-cuda
make docker-dev-jetson
make docker-dev-stm32
# ... etc
```

### Interactive Shells

```bash
make shell-dev            # CPU development
make shell-dev-cuda       # CUDA development
make shell-dev-jetson     # Jetson cross-compile
make shell-dev-rpi        # Raspberry Pi cross-compile
make shell-dev-riscv64    # RISC-V cross-compile
make shell-dev-zephyr     # Zephyr RTOS
make shell-dev-stm32      # STM32 development
make shell-dev-esp32      # ESP32 development
make shell-dev-pico       # Pico development
make shell-dev-arduino    # Arduino development
make shell-dev-atmega328pb # ATmega328PB development
make shell-dev-pic32      # PIC32 development
make shell-dev-c2000      # TI C2000 development
```

### Artifact Extraction

```bash
# Build final image with all artifacts
make docker-final

# Extract tarballs
make artifacts
ls output/
# cpu-release.tar.gz
# cuda-release.tar.gz
# jetson-release.tar.gz
# stm32-firmware.tar.gz
```

### Maintenance

```bash
make docker-clean         # Remove dangling images/containers
make docker-clean-deep    # Full prune including volumes
make docker-prune         # Remove builder/final images only
make docker-disk-usage    # Show disk usage breakdown
make docker-lint          # Lint Dockerfiles with hadolint
make docker-validate      # Validate docker-compose.yml
```

## Directory Structure

```
docker/
+-- README.md                  # This file
+-- base.Dockerfile            # Shared tooling and dependencies
+-- builder.Dockerfile         # Unified parameterized builder (DEV_IMAGE + BUILD_CMD)
+-- final.Dockerfile           # Artifact packaging
\-- dev/
    +-- cpu.Dockerfile         # Native x86_64 shell
    +-- cuda.Dockerfile        # CUDA-enabled shell
    +-- jetson.Dockerfile      # Jetson cross-compile shell
    +-- rpi.Dockerfile         # Raspberry Pi shell
    +-- riscv64.Dockerfile     # RISC-V 64 shell
    +-- zephyr.Dockerfile      # Zephyr RTOS shell
    +-- stm32.Dockerfile       # STM32 shell
    +-- esp32.Dockerfile       # ESP32 shell
    +-- pico.Dockerfile        # Pico shell
    +-- arduino.Dockerfile     # Arduino shell
    +-- atmega328pb.Dockerfile # ATmega328PB shell
    +-- pic32.Dockerfile       # PIC32 shell
    \-- c2000.Dockerfile       # TI C2000 shell
```

Toolchain setup is embedded in each dev Dockerfile. All builders use the
single `builder.Dockerfile` with `DEV_IMAGE` and `BUILD_CMD` args to select
the target platform (see `docker-compose.yml` for the per-service configuration).

## Configuration

### Environment Variables

| Variable   | Default      | Description        |
| ---------- | ------------ | ------------------ |
| `USER`     | Current user | Container username |
| `HOST_UID` | Current UID  | Container user ID  |
| `HOST_GID` | Current GID  | Container group ID |

### Volume Mounts

All dev shells mount:

| Host Path              | Container Path          | Purpose                    |
| ---------------------- | ----------------------- | -------------------------- |
| `.` (project root)     | `/home/$USER/workspace` | Source code                |
| `apex-ccache` (volume) | `/ccache`               | Compiler cache persistence |

### Device Access

Embedded dev shells include USB device passthrough:

```yaml
devices:
  - /dev/bus/usb:/dev/bus/usb
privileged: true
```

## Usage Examples

### Native Development

```bash
make shell-dev

# Inside container
[CPU] user@apex-dev-cpu:~/workspace $ make debug
[CPU] user@apex-dev-cpu:~/workspace $ make testp
```

### CUDA Development

```bash
# Requires NVIDIA Container Toolkit
make shell-dev-cuda

# Inside container
[CUDA] user@apex-dev-cuda:~/workspace $ nvcc --version
[CUDA] user@apex-dev-cuda:~/workspace $ make release
```

### Cross-Compilation (Jetson)

```bash
make shell-dev-jetson

# Inside container
[JETSON] user@apex-dev-jetson:~/workspace $ make jetson-release
[JETSON] user@apex-dev-jetson:~/workspace $ file build/jetson-aarch64-release/bin/myapp
# ELF 64-bit LSB executable, ARM aarch64
```

### Embedded Development (STM32)

```bash
make shell-dev-stm32

# Inside container
[STM32] user@apex-dev-stm32:~/workspace $ make stm32
[STM32] user@apex-dev-stm32:~/workspace $ make stm32-flash STM32_FIRMWARE=my_firmware
```

### ESP32 Development

```bash
make shell-dev-esp32

# Inside container
[ESP32] user@apex-dev-esp32:~/workspace $ make esp32
[ESP32] user@apex-dev-esp32:~/workspace $ make esp32-flash ESP32_FIRMWARE=my_firmware ESP32_PORT=/dev/ttyUSB0
```

### Pico Development

```bash
make shell-dev-pico

# Inside container
[PICO] user@apex-dev-pico:~/workspace $ make pico
[PICO] user@apex-dev-pico:~/workspace $ make pico-flash PICO_FIRMWARE=my_firmware
```

## Toolchain Details

### Cross-Compilation Targets

| Target       | Toolchain             | Architecture | ABI   |
| ------------ | --------------------- | ------------ | ----- |
| Jetson       | aarch64-linux-gnu-gcc | ARMv8-A      | LP64  |
| Raspberry Pi | aarch64-linux-gnu-gcc | ARMv8-A      | LP64  |
| RISC-V       | riscv64-linux-gnu-gcc | RV64GC       | LP64D |

### Embedded Toolchains

| Target      | Compiler                | Flash Tool        | SDK/Framework |
| ----------- | ----------------------- | ----------------- | ------------- |
| STM32       | arm-none-eabi-gcc       | st-flash, openocd | STM32Cube HAL |
| ESP32       | xtensa-esp32s3-elf-gcc  | esptool.py        | ESP-IDF v5.5  |
| Pico        | arm-none-eabi-gcc (ARM) | picotool          | Pico SDK 2.x  |
| Arduino     | avr-gcc (Arduino CLI)   | avrdude           | arduino-cli   |
| ATmega328PB | avr-gcc (system)        | avrdude           | avr-libc      |
| PIC32       | xc32-gcc                | pic32prog         | XC32          |
| C2000       | cl2000                  | UniFlash          | C2000Ware     |
| Zephyr      | Multi-arch (SDK)        | Various           | Zephyr SDK    |

## Troubleshooting

### CUDA Container Won't Start

Ensure NVIDIA Container Toolkit is installed:

```bash
# Check runtime
docker info | grep -i nvidia

# Install if missing
distribution=$(. /etc/os-release; echo $ID$VERSION_ID)
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg
curl -s -L https://nvidia.github.io/libnvidia-container/$distribution/libnvidia-container.list | \
  sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
  sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list
sudo apt update && sudo apt install -y nvidia-container-toolkit
sudo systemctl restart docker
```

### USB Device Not Accessible

Add user to dialout group (host machine):

```bash
sudo usermod -aG dialout $USER
# Log out and back in
```

Or run container with explicit device:

```bash
docker run --rm -it --device=/dev/ttyUSB0:/dev/ttyUSB0 apex.dev.stm32 bash
```

### Build Cache Too Large

```bash
# Check usage
make docker-disk-usage

# Clean build cache (often the largest consumer)
docker builder prune

# Clean unused images
docker image prune -a
```

### Proprietary Installer Fails (XC32, etc.)

Some vendor installers don't work in headless containers:

1. Install on host and mount into container
2. Build image interactively and commit
3. Use vendor's official Docker images as base

### Permission Denied on Mounted Volume

Ensure UID/GID match:

```bash
# Check your IDs
id

# Rebuild with correct IDs
HOST_UID=$(id -u) HOST_GID=$(id -g) make docker-dev
```

### Slow Builds

1. Ensure ccache volume is mounted (check `docker-compose.yml`)
2. Verify ccache is being used: `ccache -s` inside container
3. Consider building only needed images instead of `docker-all`

## Build Performance

### Layer Caching

Images share common layers through the base image. Building `apex.dev.stm32`
after `apex.dev.cpu` reuses all base layers.

### BuildKit Cache Mounts

All Dockerfiles use BuildKit cache mounts for apt and pip:

```dockerfile
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y ...
```

### ccache Persistence

The `apex-ccache` named volume persists compiler cache across container runs:

```bash
# Check ccache stats inside container
ccache -s

# Clear if needed
ccache -C
```

## Adding New Toolchains

1. Create `docker/dev/mytarget.Dockerfile` (extends `apex.dev.cpu` or `apex.base`)
2. Add the dev service to `docker-compose.yml`
3. Add the dev target to `mk/docker.mk` (use `_dev_target` template)
4. Optionally add a builder service to `docker-compose.yml` (uses `builder.Dockerfile`)
5. Optionally add a CMake toolchain file in `cmake/toolchains/`
6. Optionally add a CMake preset in `CMakePresets.json`

See existing dev Dockerfiles for reference patterns.
