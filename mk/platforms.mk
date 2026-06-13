# ==============================================================================
# mk/platforms.mk - Single source of truth for target platforms
#
# One row per platform. Every platform-derived list and target in the build is
# generated from this table -- add a platform here and nowhere else. Consumers:
#   - docker.mk : DEV_SERVICES, BUILDER_TARGETS, ARTIFACT_NAME/_DIR, _dev_base
#   - Makefile  : per-platform build/configure targets (_platform_targets)
#   - compose.mk: compose-<platform> wrappers
#   - release.mk: PLATFORM_<p>_SERVICE/BUILD/DIR
#   - CI        : print-platform-matrix (docker-images.yml image graph)
#
# Fields (P_<name>_<field>):
#   ROLE      host | cross-ship | cross-port | fw-ship | skeleton
#   SERVICE   docker compose dev service name
#   BASE      base-image keyword: base | cpu | cuda   (-> _dev_base)
#   PRESET    primary CMake preset (release / relwithdebinfo)   [non-skeleton]
#   DEBUG     debug CMake preset                                [where one exists]
#   TARGET    make-target stem (debug/release handled specially for host)
#   BUILDTGT  restricted `cmake --build --target` (c2000 -> firmware; else empty)
#   ARTIFACT  release tarball arch name                         [builder rows]
#
# A row ships a release artifact ("builder") iff ROLE != skeleton. DIR is always
# derived as build/$(P_<name>_PRESET); ARTIFACT_DIR_<t> is the preset name.
# Lists are sets: every consumer foreach-es the whole set, so order is inert.
# ==============================================================================

ifndef PLATFORMS_MK_GUARD
PLATFORMS_MK_GUARD := 1

PLATFORMS := cpu cuda jetson rpi riscv64 stm32 arduino pico esp32 c2000 \
             zephyr atmega328pb pic32

# --- host (built + released; tests run here; no toolchain) -------------------
P_cpu_ROLE      := host
P_cpu_SERVICE   := dev
P_cpu_BASE      := base
P_cpu_PRESET    := hosted-x86_64-release
P_cpu_ARTIFACT  := x86_64-linux

P_cuda_ROLE     := host
P_cuda_SERVICE  := dev-cuda
P_cuda_BASE     := base
P_cuda_PRESET   := hosted-x86_64-release
P_cuda_ARTIFACT := x86_64-linux-cuda

# --- cross-linux, shipping demos --------------------------------------------
P_jetson_ROLE     := cross-ship
P_jetson_SERVICE  := dev-jetson
P_jetson_BASE     := cuda
P_jetson_PRESET   := cross-jetson-release
P_jetson_DEBUG    := cross-jetson-debug
P_jetson_TARGET   := jetson-release
P_jetson_ARTIFACT := aarch64-jetson

P_rpi_ROLE     := cross-ship
P_rpi_SERVICE  := dev-rpi
P_rpi_BASE     := cpu
P_rpi_PRESET   := cross-rpi-release
P_rpi_DEBUG    := cross-rpi-debug
P_rpi_TARGET   := rpi-release
P_rpi_ARTIFACT := aarch64-rpi

# --- cross-linux, portability canary (built + CI, no shipping demo) ---------
P_riscv64_ROLE     := cross-port
P_riscv64_SERVICE  := dev-riscv64
P_riscv64_BASE     := cpu
P_riscv64_PRESET   := cross-riscv64-release
P_riscv64_DEBUG    := cross-riscv64-debug
P_riscv64_TARGET   := riscv-release
P_riscv64_ARTIFACT := riscv64-linux

# --- firmware, shipping demos -----------------------------------------------
P_stm32_ROLE     := fw-ship
P_stm32_SERVICE  := dev-stm32
P_stm32_BASE     := cpu
P_stm32_PRESET   := mcu-stm32-relwithdebinfo
P_stm32_DEBUG    := mcu-stm32-debug
P_stm32_TARGET   := stm32
P_stm32_ARTIFACT := stm32

P_arduino_ROLE     := fw-ship
P_arduino_SERVICE  := dev-arduino
P_arduino_BASE     := cpu
P_arduino_PRESET   := mcu-arduino-relwithdebinfo
P_arduino_DEBUG    := mcu-arduino-debug
P_arduino_TARGET   := arduino
P_arduino_ARTIFACT := arduino

P_pico_ROLE     := fw-ship
P_pico_SERVICE  := dev-pico
P_pico_BASE     := cpu
P_pico_PRESET   := mcu-pico-relwithdebinfo
P_pico_DEBUG    := mcu-pico-debug
P_pico_TARGET   := pico
P_pico_ARTIFACT := pico

P_esp32_ROLE     := fw-ship
P_esp32_SERVICE  := dev-esp32
P_esp32_BASE     := cpu
P_esp32_PRESET   := mcu-esp32-relwithdebinfo
P_esp32_DEBUG    := mcu-esp32-debug
P_esp32_TARGET   := esp32
P_esp32_ARTIFACT := esp32

P_c2000_ROLE     := fw-ship
P_c2000_SERVICE  := dev-c2000
P_c2000_BASE     := cpu
P_c2000_PRESET   := mcu-c2000-relwithdebinfo
P_c2000_DEBUG    := mcu-c2000-debug
P_c2000_TARGET   := c2000
P_c2000_BUILDTGT := firmware
P_c2000_ARTIFACT := c2000

# --- dev-shell-only skeletons (no build/test/release/CI; promote by role) ----
P_zephyr_ROLE         := skeleton
P_zephyr_SERVICE      := dev-zephyr
P_zephyr_BASE         := cpu

P_atmega328pb_ROLE    := skeleton
P_atmega328pb_SERVICE := dev-atmega328pb
P_atmega328pb_BASE    := cpu

P_pic32_ROLE          := skeleton
P_pic32_SERVICE       := dev-pic32
P_pic32_BASE          := cpu

# ------------------------------------------------------------------------------
# Derived helpers
# ------------------------------------------------------------------------------

# Builder rows = everything that ships a release artifact (non-skeleton).
PLAT_BUILDERS := $(strip $(foreach p,$(PLATFORMS),$(if $(filter-out skeleton,$(P_$(p)_ROLE)),$(p))))

endif  # PLATFORMS_MK_GUARD
