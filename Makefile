# ==============================================================================
# Makefile - Apex CSF project entry point
#
# Provides native builds, cross-compilation targets, and aggregates
# functionality from mk/*.mk modules. Run `make help` for available targets.
# ==============================================================================

.DEFAULT_GOAL := debug
MAKEFLAGS += --no-print-directory

# ==============================================================================
# Includes
# ==============================================================================

include mk/common.mk
include mk/platforms.mk
include mk/test.mk
include mk/coverage.mk
include mk/docker.mk
include mk/format.mk
include mk/sanitizers.mk
include mk/tools.mk
include mk/security.mk
include mk/checks.mk
include mk/firmware.mk
include mk/compose.mk
include mk/release.mk
include mk/size.mk
include mk/profile.mk
include mk/clean.mk

# ==============================================================================
# Configuration
# ==============================================================================

# CMake verbosity (set VERBOSE=1 for per-target details)
VERBOSE ?= 0
ifeq ($(VERBOSE),1)
  CMAKE_VERBOSE_FLAG := -DAPEX_TARGETS_VERBOSE=ON
else
  CMAKE_VERBOSE_FLAG :=
endif

# Extra CMake args (e.g., make stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON")
CMAKE_EXTRA_ARGS ?=

# Install pre-commit hooks during `prep` (make PRE_COMMIT_INSTALL=yes prep)
PRE_COMMIT_INSTALL ?= no

# ------------------------------------------------------------------------------
# CMake Preset Names (must match CMakePresets.json)
# ------------------------------------------------------------------------------

# Native x86_64 + CUDA host presets. Host targets (debug/release, cuda-*) are
# hand-written below; cross and firmware targets derive their presets from the
# platform registry (mk/platforms.mk), so only the host presets live here.
HOST_DEBUG_PRESET        ?= hosted-x86_64-debug
HOST_RELEASE_PRESET      ?= hosted-x86_64-release
HOST_CUDA_DEBUG_PRESET   ?= hosted-x86_64-cuda-debug
HOST_CUDA_RELEASE_PRESET ?= hosted-x86_64-cuda-release

# ------------------------------------------------------------------------------
# Build Directories
# ------------------------------------------------------------------------------

HOST_DEBUG_DIR        := build/$(HOST_DEBUG_PRESET)
HOST_RELEASE_DIR      := build/$(HOST_RELEASE_PRESET)
HOST_CUDA_DEBUG_DIR   := build/$(HOST_CUDA_DEBUG_PRESET)
HOST_CUDA_RELEASE_DIR := build/$(HOST_CUDA_RELEASE_PRESET)

# Firmware build-dir shorthands used by mk/size.mk and mk/firmware.mk, derived
# from the platform registry.
STM32_DIR   := build/$(P_stm32_PRESET)
ARDUINO_DIR := build/$(P_arduino_PRESET)
PICO_DIR    := build/$(P_pico_PRESET)
ESP32_DIR   := build/$(P_esp32_PRESET)
C2000_DIR   := build/$(P_c2000_PRESET)

# ==============================================================================
# Build Macros
# ==============================================================================

# _build: Configure and build a CMake preset
# Usage: $(call _build,display_name,preset,build_dir[,cmake_target])
# $(4) optional: restrict the build to one cmake target (e.g. c2000 -> firmware).
define _build
	$(call log,build,Configuring $(1))
	@cmake --preset $(2) $(CMAKE_VERBOSE_FLAG) $(CMAKE_EXTRA_ARGS)
	$(call log,build,Building $(1))
	@cmake --build --preset $(2)$(if $(4), --target $(4)) -j$(NUM_JOBS)
	@ln -sf $(3)/compile_commands.json compile_commands.json
endef

# _configure: Configure a CMake preset (no build)
# Usage: $(call _configure,preset,build_dir)
define _configure
	@cmake --preset $(1) $(CMAKE_VERBOSE_FLAG) $(CMAKE_EXTRA_ARGS)
	@ln -sf $(2)/compile_commands.json compile_commands.json
endef

# _platform_targets: Generate build and configure targets for a platform
# $(1) = target name, $(2) = display name, $(3) = preset, $(4) = build dir,
# $(5) = optional cmake target to restrict the build to.
define _platform_targets
.PHONY: $(1) configure-$(1)
$(1): prep
	$$(call _build,$(2),$(3),$(4),$(5))
configure-$(1): prep
	$$(call _configure,$(3),$(4))
endef

# ==============================================================================
# Help
# ==============================================================================

help:
	@printf '%s\n' "Apex CSF Build System"
	@printf '%s\n' "====================="
	@printf '\n'
	@printf '%s\n' "Native Builds:"
	@printf '  %-28s %s\n' "make debug" "Build native debug (default; CUDA auto-detect)"
	@printf '  %-28s %s\n' "make release" "Build native release (CUDA auto-detect)"
	@printf '  %-28s %s\n' "make cuda-debug" "Build native debug requiring CUDA"
	@printf '  %-28s %s\n' "make cuda-release" "Build native release requiring CUDA"
	@printf '  %-28s %s\n' "make docs" "Build Doxygen documentation"
	@printf '  %-28s %s\n' "make configure" "Configure only (no build)"
	@printf '\n'
	@printf '%s\n' "Cross-Compilation:"
	@printf '  %-28s %s\n' "make jetson-debug" "Build for Jetson (aarch64 + CUDA)"
	@printf '  %-28s %s\n' "make jetson-release" "Build for Jetson release"
	@printf '  %-28s %s\n' "make rpi-debug" "Build for Raspberry Pi (aarch64)"
	@printf '  %-28s %s\n' "make rpi-release" "Build for Raspberry Pi release"
	@printf '  %-28s %s\n' "make riscv-debug" "Build for RISC-V 64"
	@printf '  %-28s %s\n' "make riscv-release" "Build for RISC-V 64 release"
	@printf '  %-28s %s\n' "make stm32" "Build STM32 firmware (RelWithDebInfo)"
	@printf '  %-28s %s\n' "make stm32-debug" "Build STM32 firmware (debug, -Og -g3)"
	@printf '  %-28s %s\n' "make arduino" "Build Arduino firmware (RelWithDebInfo)"
	@printf '  %-28s %s\n' "make arduino-debug" "Build Arduino firmware (debug, -Og -g3)"
	@printf '  %-28s %s\n' "make pico" "Build Pico (RP2040) firmware (RelWithDebInfo)"
	@printf '  %-28s %s\n' "make pico-debug" "Build Pico firmware (debug, -Og -g3)"
	@printf '  %-28s %s\n' "make esp32" "Build ESP32 firmware (RelWithDebInfo)"
	@printf '  %-28s %s\n' "make esp32-debug" "Build ESP32 firmware (debug, -Og -g3)"
	@printf '  %-28s %s\n' "make c2000" "Build C2000 firmware (RelWithDebInfo)"
	@printf '  %-28s %s\n' "make c2000-debug" "Build C2000 firmware (debug, --opt_level=0)"
	@printf '\n'
	@printf '%s\n' "Testing:"
	@printf '  %-28s %s\n' "make test" "Run all C++ tests (serial)"
	@printf '  %-28s %s\n' "make testp" "Run C++ tests (parallel; CUDA set if GPU present)"
	@printf '  %-28s %s\n' "make testp-cpu" "Run C++ tests, excluding the CUDA set"
	@printf '  %-28s %s\n' "make testp-cuda" "Run only the CUDA tests (needs a GPU)"
	@printf '  %-28s %s\n' "make test-py" "Run Python tools tests"
	@printf '\n'
	@printf '%s\n' "Tools:"
	@printf '  %-28s %s\n' "make tools" "Build all tools (C++, Python, Rust)"
	@printf '  %-28s %s\n' "make tools-cpp" "Build C++ tools only"
	@printf '  %-28s %s\n' "make tools-py" "Build Python tools only"
	@printf '  %-28s %s\n' "make tools-rust" "Build Rust tools only"
	@printf '\n'
	@printf '%s\n' "Profiling (chrome://tracing format):"
	@printf '  %-28s %s\n' "make profile-configure" "CMake configure-time hotspots"
	@printf '  %-28s %s\n' "make profile-build" "Ninja build-time hotspots (after make debug)"
	@printf '  %-28s %s\n' "make profile-clean" "Remove profile JSON output"
	@printf '\n'
	@printf '%s\n' "Quality:"
	@printf '  %-28s %s\n' "make format" "Auto-fix formatting issues"
	@printf '  %-28s %s\n' "make format-check" "Check formatting (no fixes)"
	@printf '  %-28s %s\n' "make coverage" "Generate code coverage report"
	@printf '  %-28s %s\n' "make static" "Run static analysis (clang-tidy)"
	@printf '  %-28s %s\n' "make asan" "Build and test with AddressSanitizer"
	@printf '  %-28s %s\n' "make tsan" "Build and test with ThreadSanitizer"
	@printf '  %-28s %s\n' "make ubsan" "Build and test with UBSanitizer"
	@printf '  %-28s %s\n' "make checks-nightly" "Run every check the nightly runs"
	@printf '  %-28s %s\n' "make list-checks" "List all checks by category and CI tier"
	@printf '\n'
	@printf '%s\n' "Integration Artifacts:"
	@printf '  %-28s %s\n' "make ops-artifacts" "Generate all ops artifacts (struct dicts + templates + deck)"
	@printf '  %-28s %s\n' "make apex-data-db" "Generate JSON struct dictionaries"
	@printf '  %-28s %s\n' "make tprm-templates" "Generate TOML templates for tunable params"
	@printf '  %-28s %s\n' "make ops-deck" "Generate consolidated cmd/tlm deck"
	@printf '  %-28s %s\n' "make ops-sdk APP=<name>" "Package ops SDK for external integrators"
	@printf '  %-28s %s\n' "make zenith-target APP=<name>" "Generate Zenith target config directory"
	@printf '\n'
	@printf '%s\n' "Release:"
	@printf '  %-28s %s\n' "make release APP=<name>" "Build + package all platforms for an app"
	@printf '  %-28s %s\n' "make release-all" "Release all registered apps"
	@printf '  %-28s %s\n' "make release-clean" "Remove release/ directory"
	@printf '\n'
	@printf '%s\n' "Size Analysis (bloaty):"
	@printf '  %-28s %s\n' "make size FILE=<path>" "Bloaty size breakdown of any binary"
	@printf '  %-28s %s\n' "make size-stm32 FW=<n>" "Bloaty on stm32 firmware ELF"
	@printf '  %-28s %s\n' "make size-arduino FW=<n>" "Bloaty on arduino firmware ELF"
	@printf '  %-28s %s\n' "make size-pico FW=<n>" "Bloaty on pico firmware ELF"
	@printf '  %-28s %s\n' "make size-esp32 FW=<n>" "Bloaty on esp32 firmware ELF"
	@printf '  %-28s %s\n' "make size-c2000 FW=<n>" "Bloaty on c2000 firmware ELF"
	@printf '  %-28s %s\n' "make size-app APP=<n>" "Bloaty on native release app"
	@printf '  %-28s %s\n' "make size-diff NEW=. OLD=." "Bloaty diff between two binaries"
	@printf '\n'
	@printf '%s\n' "Firmware:"
	@printf '  %-28s %s\n' "make stm32-flash" "Flash STM32 firmware via ST-Link"
	@printf '  %-28s %s\n' "make stm32-reset" "Reset STM32 via ST-Link"
	@printf '  %-28s %s\n' "make arduino-flash" "Flash Arduino firmware via avrdude"
	@printf '  %-28s %s\n' "make arduino-reset" "Reset Arduino via serial DTR"
	@printf '  %-28s %s\n' "make pico-flash" "Flash Pico firmware via picotool"
	@printf '  %-28s %s\n' "make pico-reset" "Reset Pico via picotool"
	@printf '  %-28s %s\n' "make esp32-flash" "Flash ESP32 firmware via esptool"
	@printf '  %-28s %s\n' "make esp32-reset" "Reset ESP32 via esptool"
	@printf '  %-28s %s\n' "make c2000-flash" "Flash C2000 firmware via UniFlash"
	@printf '  %-28s %s\n' "make c2000-reset" "Reset C2000 via UniFlash"
	@printf '\n'
	@printf '%s\n' "Compose (build via Docker Compose):"
	@printf '  %-28s %s\n' "make compose-debug" "Native debug via dev-cuda"
	@printf '  %-28s %s\n' "make compose-release" "Native release via dev-cuda"
	@printf '  %-28s %s\n' "make compose-test" "Run tests via dev-cuda"
	@printf '  %-28s %s\n' "make compose-testp" "Run tests (parallel) via dev-cuda"
	@printf '  %-28s %s\n' "make compose-coverage" "Coverage report via dev-cuda"
	@printf '  %-28s %s\n' "make compose-format" "Format code via dev-cuda"
	@printf '  %-28s %s\n' "make compose-tools" "Build all tools via dev-cuda"
	@printf '  %-28s %s\n' "make compose-bench" "Run Vernier bench inside the dev container; pass BENCH_ARGS='...'"
	@printf '  %-28s %s\n' "make compose-stm32" "STM32 firmware via dev-stm32"
	@printf '  %-28s %s\n' "make compose-stm32-flash" "Flash STM32 via dev-stm32"
	@printf '  %-28s %s\n' "make compose-arduino" "Arduino firmware via dev-arduino"
	@printf '  %-28s %s\n' "make compose-arduino-flash" "Flash Arduino via dev-arduino"
	@printf '  %-28s %s\n' "make compose-pico" "Pico firmware via dev-pico"
	@printf '  %-28s %s\n' "make compose-pico-flash" "Flash Pico via dev-pico"
	@printf '  %-28s %s\n' "make compose-esp32" "ESP32 firmware via dev-esp32"
	@printf '  %-28s %s\n' "make compose-esp32-flash" "Flash ESP32 via dev-esp32"
	@printf '  %-28s %s\n' "make compose-c2000" "C2000 firmware via dev-c2000"
	@printf '  %-28s %s\n' "make compose-c2000-flash" "Flash C2000 via dev-c2000"
	@printf '  %-28s %s\n' "make compose-jetson-debug" "Jetson debug via dev-jetson"
	@printf '  %-28s %s\n' "make compose-jetson-release" "Jetson release via dev-jetson"
	@printf '  %-28s %s\n' "make compose-rpi-debug" "RPi debug via dev-rpi"
	@printf '  %-28s %s\n' "make compose-riscv-debug" "RISC-V debug via dev-riscv64"
	@printf '\n'
	@printf '%s\n' "Docker:"
	@printf '  %-28s %s\n' "make shell-dev" "Enter CPU development shell"
	@printf '  %-28s %s\n' "make shell-dev-cuda" "Enter CUDA development shell"
	@printf '  %-28s %s\n' "make shell-dev-jetson" "Enter Jetson cross-compile shell"
	@printf '  %-28s %s\n' "make docker-all" "Build all Docker images"
	@printf '  %-28s %s\n' "make artifacts" "Extract release artifacts"
	@printf '\n'
	@printf '%s\n' "Cleanup:"
	@printf '  %-28s %s\n' "make clean" "Clean build artifacts"
	@printf '  %-28s %s\n' "make distclean" "Remove build/ entirely (SURE=1 if cross-builds present)"
	@printf '  %-28s %s\n' "make docker-clean" "Clean Docker dangling images"
	@printf '  %-28s %s\n' "make docker-prune" "Remove all apex.* images"
	@printf '\n'
	@printf '%s\n' "Utilities:"
	@printf '  %-28s %s\n' "make ccache-stats" "Show ccache statistics"
	@printf '  %-28s %s\n' "make ccache-clear" "Clear ccache"
	@printf '  %-28s %s\n' "make docker-disk-usage" "Show Docker disk usage"
	@printf '\n'
	@printf '%s\n' "Variables:"
	@printf '  %-28s %s\n' "VERBOSE=1" "Enable verbose CMake output"
	@printf '  %-28s %s\n' "NUM_JOBS=N" "Override parallel job count (current: $(NUM_JOBS))"
	@printf '  %-28s %s\n' "CMAKE_EXTRA_ARGS=\"...\"" "Pass extra CMake arguments"
	@printf '  %-28s %s\n' "BUILD_DIR=path" "Override build directory"

# ==============================================================================
# Prep
# ==============================================================================

prep:
	@mkdir -p "$(BUILD_DIR)"
	@touch "$(BUILD_DIR)/.env"
	@if [ "$(PRE_COMMIT_INSTALL)" = "yes" ]; then \
	  printf '[prep] Installing pre-commit hooks\n'; \
	  pre-commit install; \
	fi

# ==============================================================================
# Native Host Builds (x86_64)
# ==============================================================================

debug: prep
	$(call _build,native debug,$(HOST_DEBUG_PRESET),$(HOST_DEBUG_DIR))

docs: prep
	$(call log,build,Building documentation)
	@cmake --preset $(HOST_DEBUG_PRESET) $(CMAKE_VERBOSE_FLAG)
	@cmake --build --preset $(HOST_DEBUG_PRESET) --target docs -j$(NUM_JOBS)

# Same as `docs` but fails if doxygen emits any warning. Use as the CI gate.
docs-check: prep
	$(call log,build,Checking documentation (zero-warning gate))
	@cmake --preset $(HOST_DEBUG_PRESET) $(CMAKE_VERBOSE_FLAG)
	@cmake --build --preset $(HOST_DEBUG_PRESET) --target docs -j$(NUM_JOBS) 2>&1 | tee "$(HOST_DEBUG_DIR)/.docs-check.log"
	@bad=$$(grep -E '^/[^:]+: ?(warning:|error:)|^[^/]*: ?warning:' "$(HOST_DEBUG_DIR)/.docs-check.log" | grep -v -E '^/usr/|/_deps/' || true); \
	  if [ -n "$$bad" ]; then \
	    printf '$(TERM_RED)[docs-check]$(TERM_RESET) doxygen emitted warnings:\n%s\n' "$$bad"; \
	    exit 1; \
	  fi
	$(call log,docs-check,OK -- zero doxygen warnings)

# ==============================================================================
# Cross-Compilation and Firmware Builds (generated from templates)
# ==============================================================================

$(eval $(call _platform_targets,cuda-debug,native CUDA debug,$(HOST_CUDA_DEBUG_PRESET),$(HOST_CUDA_DEBUG_DIR)))
$(eval $(call _platform_targets,cuda-release,native CUDA release,$(HOST_CUDA_RELEASE_PRESET),$(HOST_CUDA_RELEASE_DIR)))
# Cross-compile + firmware build/configure targets, generated from the platform
# registry (mk/platforms.mk): each emits <target>, <target>-debug, and their
# configure- variants. c2000 restricts the build to its firmware target via the
# BUILDTGT field (was a hand-written special case). The debug target name is the
# primary with -release stripped and -debug appended. Host stays hand-written
# above: cuda's dev-target preset differs from its release-artifact preset, and
# cpu is the default-goal debug/release.
$(foreach p,$(filter-out cpu cuda,$(PLAT_BUILDERS)),\
  $(eval $(call _platform_targets,$(P_$(p)_TARGET),$(P_$(p)_DISPLAY),$(P_$(p)_PRESET),build/$(P_$(p)_PRESET),$(P_$(p)_BUILDTGT)))\
  $(eval $(call _platform_targets,$(patsubst %-release,%,$(P_$(p)_TARGET))-debug,$(P_$(p)_DISPLAY) (debug),$(P_$(p)_DEBUG),build/$(P_$(p)_DEBUG),$(P_$(p)_BUILDTGT))))

# ==============================================================================
# Configure-Only (native)
# ==============================================================================

configure: prep
	$(call _configure,$(HOST_DEBUG_PRESET),$(HOST_DEBUG_DIR))

configure-release: prep
	$(call _configure,$(HOST_RELEASE_PRESET),$(HOST_RELEASE_DIR))

# ==============================================================================
# Utilities
# ==============================================================================

ccache-stats:
	@if command -v ccache >/dev/null 2>&1; then \
	  ccache -s; \
	else \
	  printf '[ccache] Not installed\n'; \
	fi

ccache-clear:
	@if command -v ccache >/dev/null 2>&1; then \
	  ccache -C; \
	  printf '[ccache] Cache cleared\n'; \
	else \
	  printf '[ccache] Not installed\n'; \
	fi

# ==============================================================================
# Phony Declarations
# ==============================================================================

.PHONY: help prep debug docs docs-check
.PHONY: configure configure-release
.PHONY: ccache-stats ccache-clear
