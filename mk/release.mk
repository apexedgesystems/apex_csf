# ==============================================================================
# mk/release.mk - Unified release packaging infrastructure
#
# Builds all target platforms for an app, packages each (ELF dependency
# resolution for POSIX, firmware copy for bare-metal), and aggregates into
# a single release directory with a combined tarball.
#
# Usage:
#   make release APP=<name>            Build + package all platforms
#   make release-all                   Release all registered apps
#   make release-clean                 Remove release/ directory
#
# Output (ApexFileSystem layout):
#   release/<APP>/<platform>/bank_a/bin/<binary>       POSIX executable
#   release/<APP>/<platform>/bank_a/libs/*.so           Shared library dependencies
#   release/<APP>/<platform>/bank_a/tprm/master.tprm   TPRM configuration
#   release/<APP>/<platform>/run.sh                    Launch script
#   release/<APP>/<platform>/firmware/<name>.*          Bare-metal firmware
#   release/<APP>.tar.gz                               Combined tarball
#
# How it works:
#   1. For each platform in the app's manifest:
#      a. Build via Docker Compose (reuses existing compose build targets)
#      b. POSIX: build + package in one step via ninja package_<APP>
#         (pkg_resolve.sh resolves ELF deps, stages bin/lib/tprm/run.sh)
#      c. Firmware: copy .elf/.bin/.hex from build dir
#   2. Aggregate all platforms into release/<APP>/
#   3. Create combined tarball
#
# App manifests:
#   Each app provides its own release manifest at apps/<app>/release.mk.
#   The manifest registers the app and declares its target platforms:
#
#     APP_REGISTRY += MyApp
#     APP_MyApp_PLATFORMS              := rpi stm32
#     APP_MyApp_TPRM                   := apps/my_app/tprm/master.tprm
#     APP_MyApp_rpi_TYPE               := posix
#     APP_MyApp_rpi_BINARY             := MyApp
#     APP_MyApp_stm32_TYPE             := firmware
#     APP_MyApp_stm32_BINARY           := my_firmware
#
#   TYPE=posix:    ELF executable with shared library dependencies.
#                  Uses pkg_resolve.sh (via CMake package_<APP> target) for
#                  BFS dependency resolution against the build lib/ directory.
#                  Includes TPRM config and a launch script in the package.
#
#   TYPE=firmware: Bare-metal firmware with no shared library dependencies.
#                  Copies .elf/.bin/.hex from the build firmware/ directory.
#
# Adding a new platform:
#   1. Add PLATFORM_<name>_SERVICE, _BUILD, _DIR entries below
#   2. Ensure the Docker Compose service and Make build target exist
#
# ==============================================================================

ifndef RELEASE_MK_GUARD
RELEASE_MK_GUARD := 1

# APP must be provided for release/package targets
APP ?=

# ==============================================================================
# Low-Level Packaging (runs inside Docker container)
# ==============================================================================
# Invoked by the POSIX release template via _compose_run. Calls pkg_resolve.sh
# directly for ELF dependency resolution, TPRM staging, and launch script
# generation. TPRM path comes from the app's release manifest (APP_<name>_TPRM).
# ==============================================================================

TPRM ?=
EXTRA_BINS ?=

package:
	@test -n "$(APP)" || \
	  { printf '$(TERM_RED)[package]$(TERM_RESET) APP not set. Usage: make package APP=<name>\n'; exit 1; }
	@test -d "$(BUILD_DIR)" || \
	  { printf '$(TERM_RED)[package]$(TERM_RESET) BUILD_DIR not found: $(BUILD_DIR)\n'; exit 1; }
	@bash tools/sh/bin/pkg_resolve.sh --app "$(APP)" --build-dir "$(BUILD_DIR)" \
	  $(if $(TPRM),--tprm "$(TPRM)") \
	  $(foreach bin,$(EXTRA_BINS),--extra-bin "$(bin)")

package-clean:
	$(call log,package,Cleaning packages from $(BUILD_DIR))
	@rm -rf "$(BUILD_DIR)/packages"
	$(call log_ok,package,Clean complete)

# ==============================================================================
# Platform Registry
# ==============================================================================
# Maps platform short names to Docker Compose service, Make build target,
# and build directory. Build directories reuse the variables from Makefile
# (HOST_RELEASE_DIR, RPI_RELEASE_DIR, etc.) for single-source-of-truth.
# ==============================================================================

PLATFORM_native_SERVICE  := dev-cuda
PLATFORM_native_BUILD    := release
PLATFORM_native_DIR       = $(HOST_RELEASE_DIR)

PLATFORM_rpi_SERVICE     := dev-rpi
PLATFORM_rpi_BUILD       := rpi-release
PLATFORM_rpi_DIR          = $(RPI_RELEASE_DIR)

PLATFORM_stm32_SERVICE   := dev-stm32
PLATFORM_stm32_BUILD     := stm32
PLATFORM_stm32_DIR        = $(STM32_DIR)

PLATFORM_jetson_SERVICE  := dev-jetson
PLATFORM_jetson_BUILD    := jetson-release
PLATFORM_jetson_DIR       = $(JETSON_RELEASE_DIR)

PLATFORM_riscv64_SERVICE := dev-riscv64
PLATFORM_riscv64_BUILD   := riscv64-release
PLATFORM_riscv64_DIR      = $(RISCV_RELEASE_DIR)

PLATFORM_c2000_SERVICE   := dev-c2000
PLATFORM_c2000_BUILD     := c2000
PLATFORM_c2000_DIR        = $(C2000_DIR)

PLATFORM_arduino_SERVICE := dev-arduino
PLATFORM_arduino_BUILD   := arduino
PLATFORM_arduino_DIR      = $(ARDUINO_DIR)

PLATFORM_pico_SERVICE    := dev-pico
PLATFORM_pico_BUILD      := pico
PLATFORM_pico_DIR         = $(PICO_DIR)

PLATFORM_esp32_SERVICE   := dev-esp32
PLATFORM_esp32_BUILD     := esp32
PLATFORM_esp32_DIR        = $(ESP32_DIR)

# ==============================================================================
# App Manifests (auto-discovered from apps/*/release.mk)
# ==============================================================================
# Each app provides its own manifest. Manifests append to APP_REGISTRY and
# define APP_<Name>_PLATFORMS, APP_<Name>_<plat>_TYPE, APP_<Name>_<plat>_BINARY.
# ==============================================================================

APP_REGISTRY :=

-include $(wildcard apps/*/release.mk)

# ==============================================================================
# Release Directory
# ==============================================================================

RELEASE_DIR := build/release

# ==============================================================================
# Generated Targets
# ==============================================================================
# For each app/platform combination, generate a _release-<APP>-<platform>
# target using Make's $(eval). This avoids shell-level dynamic variable
# expansion which doesn't work with Make functions like _compose_run.
# ==============================================================================

# ------------------------------------------------------------------------------
# POSIX template: build -> package -> stage into release/
# $(1) = app name, $(2) = platform
#
# 1. Build via Docker Compose (cmake --build)
# 2. Package via ninja package_<APP> (pkg_resolve.sh resolves ELF deps,
#    stages bin/lib, includes TPRM, generates run.sh)
# 3. Copy staged package into release/ directory
# ------------------------------------------------------------------------------
define _RELEASE_POSIX_template
.PHONY: _release-$(1)-$(2)
_release-$(1)-$(2):
	$$(call _compose_run,release build $(2),$(PLATFORM_$(2)_SERVICE),$(PLATFORM_$(2)_BUILD))
	$$(call _compose_run,release package $(2),$(PLATFORM_$(2)_SERVICE),package,APP=$(APP_$(1)_$(2)_BINARY) BUILD_DIR=$$(PLATFORM_$(2)_DIR) TPRM=$(APP_$(1)_TPRM) EXTRA_BINS="$(APP_$(1)_$(2)_EXTRA_BINS)")
	@mkdir -p $(RELEASE_DIR)/$(1)/$(2)
	@cp -a $$(PLATFORM_$(2)_DIR)/packages/$(APP_$(1)_$(2)_BINARY)/. $(RELEASE_DIR)/$(1)/$(2)/
endef

# ------------------------------------------------------------------------------
# Firmware template: build -> copy artifacts into release/
# $(1) = app name, $(2) = platform
# ------------------------------------------------------------------------------
define _RELEASE_FIRMWARE_template
.PHONY: _release-$(1)-$(2)
_release-$(1)-$(2):
	$$(call _compose_run,release build $(2),$(PLATFORM_$(2)_SERVICE),$(PLATFORM_$(2)_BUILD))
	@mkdir -p $(RELEASE_DIR)/$(1)/$(2)/firmware
	@cp $$(PLATFORM_$(2)_DIR)/firmware/$(APP_$(1)_$(2)_BINARY).* \
	  $(RELEASE_DIR)/$(1)/$(2)/firmware/ 2>/dev/null || true
endef

# Expand templates for all app/platform combinations
$(foreach app,$(APP_REGISTRY),\
  $(foreach plat,$(APP_$(app)_PLATFORMS),\
    $(if $(filter posix,$(APP_$(app)_$(plat)_TYPE)),\
      $(eval $(call _RELEASE_POSIX_template,$(app),$(plat))),\
      $(eval $(call _RELEASE_FIRMWARE_template,$(app),$(plat))))))

# ==============================================================================
# Release Targets
# ==============================================================================

# ------------------------------------------------------------------------------
# release: Build native release (no APP) or package an app (APP=<name>)
#
# Without APP:
#   Builds the native x86_64 release (same as the old `make release`).
#
# With APP:
#   Builds all target platforms, packages each, and creates a combined tarball.
#   Outputs:
#     release/<APP>/<platform>/...    Staged artifacts per platform
#     release/<APP>.tar.gz            Combined tarball
# ------------------------------------------------------------------------------

ifdef APP
release:
	@rm -rf "$(RELEASE_DIR)/$(APP)"
	@$(MAKE) $(addprefix _release-$(APP)-,$(APP_$(APP)_PLATFORMS))
	@tar -czf "$(RELEASE_DIR)/$(APP).tar.gz" -C "$(RELEASE_DIR)" "$(APP)"
	$(call log_ok,release,$(APP).tar.gz ($$(du -sh "$(RELEASE_DIR)/$(APP).tar.gz" | cut -f1)))
else
release: prep
	$(call _build,native release,$(HOST_RELEASE_PRESET),$(HOST_RELEASE_DIR))
endif

# ------------------------------------------------------------------------------
# release-all: Release all registered apps
# ------------------------------------------------------------------------------

release-all:
	@for app in $(APP_REGISTRY); do \
	  $(MAKE) release APP=$$app || exit 1; \
	done

# ------------------------------------------------------------------------------
# release-clean: Remove all release artifacts
# ------------------------------------------------------------------------------

release-clean:
	$(call log,release,Cleaning $(RELEASE_DIR)/)
	@rm -rf "$(RELEASE_DIR)"
	$(call log_ok,release,Clean complete)

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: package package-clean
.PHONY: release release-all release-clean

endif  # RELEASE_MK_GUARD
