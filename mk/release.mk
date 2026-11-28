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
#      b. POSIX: build + package via the CMake package_<APP> target
#         (cmake --install stages bank_a/{bin,libs,tprm} + run.sh)
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
#     APP_MyApp_rpi_TYPE               := posix
#     APP_MyApp_rpi_BINARY             := MyApp
#     APP_MyApp_stm32_TYPE             := firmware
#     APP_MyApp_stm32_BINARY           := my_firmware
#     APP_MyApp_stm32_BOARD            := nucleo_l476rg   (optional)
#
#   The deployable's executives and TPRM are declared in CMake next to the app
#   (apex_add_deployment), not in this manifest.
#
#   TYPE=posix:    ELF executable with shared library dependencies. The CMake
#                  package_<APP> target stages the app, its graph-derived shared
#                  library closure, the TPRM, and a launch script into bank_a.
#
#   TYPE=firmware: Bare-metal firmware with no shared library dependencies.
#                  Copies .elf/.bin/.hex from the build firmware/ directory.
#                  A firmware built for one board names it in BOARD and is
#                  then built in that board's own directory,
#                  <platform dir>-<board>. Without BOARD the platform's
#                  shared directory is used, with whichever board it was
#                  configured for.
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
# Invoked by the POSIX release template via _compose_run. Builds the app's
# package_<APP> target, which stages the deployable bundle from CMake install
# components (bank_a/{bin,libs,tprm} + run.sh) and tars it. The executives,
# library closure, and TPRM are declared in CMake (apex_add_deployment +
# apex_finalize_packages), so nothing platform-specific is threaded through here.
# ==============================================================================

package:
	@test -n "$(APP)" || \
	  { printf '$(TERM_RED)[package]$(TERM_RESET) APP not set. Usage: make package APP=<name>\n'; exit 1; }
	@test -d "$(BUILD_DIR)" || \
	  { printf '$(TERM_RED)[package]$(TERM_RESET) BUILD_DIR not found: $(BUILD_DIR)\n'; exit 1; }
	@cmake --build "$(BUILD_DIR)" --target package_$(APP)

package-clean:
	$(call log,package,Cleaning packages from $(BUILD_DIR))
	@rm -rf "$(BUILD_DIR)/packages"
	$(call log_ok,package,Clean complete)

# ==============================================================================
# Platform Registry
# ==============================================================================
# Maps app-release platform names to Docker Compose service, Make build target,
# and build directory. The cross-compile and firmware entries derive from the
# platform registry (mk/platforms.mk). `native` is hand-written: it builds the
# host release tree (build/hosted-x86_64-release) on the dev-cuda image via the
# `release` target -- a hybrid that doesn't map to a single registry row.
# ==============================================================================

PLATFORM_native_SERVICE  := dev-cuda
PLATFORM_native_BUILD    := release
PLATFORM_native_DIR      := build/$(P_cpu_PRESET)

$(foreach p,$(filter-out cpu cuda,$(PLAT_BUILDERS)),\
  $(eval PLATFORM_$(p)_SERVICE := $(P_$(p)_SERVICE))\
  $(eval PLATFORM_$(p)_BUILD   := $(P_$(p)_TARGET))\
  $(eval PLATFORM_$(p)_DIR     := build/$(P_$(p)_PRESET)))

# The CMake variable a platform's toolchain reads its board from, for the
# platforms whose firmware apps may name one.
PLATFORM_stm32_BOARD_VAR := APEX_STM32_BOARD

# ==============================================================================
# App Manifests (auto-discovered from demos/*/release.mk and apps/*/release.mk)
# ==============================================================================
# Each app provides its own manifest. Manifests append to APP_REGISTRY and
# define APP_<Name>_PLATFORMS, APP_<Name>_<plat>_TYPE, APP_<Name>_<plat>_BINARY.
# demos/ holds the stock demos; apps/ is the user production space -- both
# release through the same manifest surface.
# ==============================================================================

APP_REGISTRY :=

-include $(wildcard demos/*/release.mk apps/*/release.mk)

# ==============================================================================
# Release Directory
# ==============================================================================

RELEASE_DIR := build/release

# ==============================================================================
# Released Apps
# ==============================================================================
# The subset of APP_REGISTRY that ships in a GitHub Release. The release.yml
# apps matrix derives from this via print-release-apps: one CI job per app,
# each pulling exactly the dev images its platforms build in.
# ==============================================================================

RELEASE_APPS := ApexEdgeDemo ApexHilDemo ApexHilSupervised

# dev-<x> compose service -> apex.dev.<x> image name
_svc_image = apex.$(subst -,.,$(1))
_app_images = $(foreach p,$(APP_$(1)_PLATFORMS),$(call _svc_image,$(PLATFORM_$(p)_SERVICE)))

# Emit RELEASE_APPS as a JSON array of {app, images} objects for the CI matrix.
print-release-apps:
	@printf '%s\n' '$(foreach a,$(RELEASE_APPS),{"app":"$(a)"$(_comma) "images":"$(strip $(call _app_images,$(a)))"})' \
	  | sed 's/} {/}, {/g; s/^/[/; s/$$/]/'

.PHONY: print-release-apps

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
# 2. Package via the CMake package_<APP> target (cmake --install stages
#    bank_a/{bin,libs,tprm} + run.sh from install components; TPRM and extra
#    bins are declared in CMake, not threaded through here)
# 3. Copy staged package into release/ directory
# ------------------------------------------------------------------------------
define _RELEASE_POSIX_template
.PHONY: _release-$(1)-$(2)
_release-$(1)-$(2):
	$$(call _compose_run,release build $(2),$(PLATFORM_$(2)_SERVICE),$(PLATFORM_$(2)_BUILD))
	$$(call _compose_run,release package $(2),$(PLATFORM_$(2)_SERVICE),package,APP=$(APP_$(1)_$(2)_BINARY) BUILD_DIR=$$(PLATFORM_$(2)_DIR))
	@mkdir -p $(RELEASE_DIR)/$(1)/$(2)
	@cp -a $$(PLATFORM_$(2)_DIR)/packages/$(APP_$(1)_$(2)_BINARY)/. $(RELEASE_DIR)/$(1)/$(2)/
endef

# ------------------------------------------------------------------------------
# Firmware template: build -> copy artifacts into release/
# $(1) = app name, $(2) = platform
#
# The image is the whole release for a firmware platform, so a build that
# produced none (a board the firmware does not target, for instance) fails here.
# ------------------------------------------------------------------------------
define _RELEASE_FIRMWARE_template
.PHONY: _release-$(1)-$(2)
_release-$(1)-$(2):
	$$(call _compose_run,release build $(2),$(PLATFORM_$(2)_SERVICE),$(PLATFORM_$(2)_BUILD))
	@mkdir -p $(RELEASE_DIR)/$(1)/$(2)/firmware
	@cp $$(PLATFORM_$(2)_DIR)/firmware/$(APP_$(1)_$(2)_BINARY).* \
	  $(RELEASE_DIR)/$(1)/$(2)/firmware/ 2>/dev/null || \
	  { printf '$(TERM_RED)[release]$(TERM_RESET) %s: no firmware image %s.* under %s/firmware\n' \
	      "$(1)/$(2)" "$(APP_$(1)_$(2)_BINARY)" "$$(PLATFORM_$(2)_DIR)"; exit 1; }
endef

# ------------------------------------------------------------------------------
# Firmware template for an app that names its board
# $(1) = app name, $(2) = platform
#
# A platform's shared build directory keeps the board it was first
# configured for, and the toolchain refuses another board there. An app
# whose firmware exists for one board only would release from that
# directory or fail to, according to what was built in it last. Its image
# is therefore configured for its board in a directory of that board's own
# and copied from there.
# ------------------------------------------------------------------------------
define _RELEASE_FIRMWARE_BOARD_template
.PHONY: _release-$(1)-$(2)
_release-$(1)-$(2):
	$$(call _compose_run,release build $(2) for $(APP_$(1)_$(2)_BOARD),$(PLATFORM_$(2)_SERVICE),_release-firmware-tree,FW_PRESET=$(P_$(2)_PRESET) FW_DIR=$(PLATFORM_$(2)_DIR)-$(APP_$(1)_$(2)_BOARD) FW_BOARD_ARG=-D$(PLATFORM_$(2)_BOARD_VAR)=$(APP_$(1)_$(2)_BOARD))
	@mkdir -p $(RELEASE_DIR)/$(1)/$(2)/firmware
	@cp $(PLATFORM_$(2)_DIR)-$(APP_$(1)_$(2)_BOARD)/firmware/$(APP_$(1)_$(2)_BINARY).* \
	  $(RELEASE_DIR)/$(1)/$(2)/firmware/ 2>/dev/null || \
	  { printf '$(TERM_RED)[release]$(TERM_RESET) %s: no firmware image %s.* under %s/firmware\n' \
	      "$(1)/$(2)" "$(APP_$(1)_$(2)_BINARY)" "$(PLATFORM_$(2)_DIR)-$(APP_$(1)_$(2)_BOARD)"; exit 1; }
endef

# In-container half of the template above: configure the platform's preset
# for one board in that board's directory, and build it.
.PHONY: _release-firmware-tree
_release-firmware-tree: prep
	@test -n "$(FW_PRESET)" && test -n "$(FW_DIR)" && test -n "$(FW_BOARD_ARG)" || \
	  { printf '[release] _release-firmware-tree needs FW_PRESET, FW_DIR and FW_BOARD_ARG\n'; exit 1; }
	$(call log,build,Configuring $(FW_PRESET) in $(FW_DIR))
	@cmake --preset $(FW_PRESET) -B $(FW_DIR) $(CMAKE_VERBOSE_FLAG) $(CMAKE_EXTRA_ARGS) $(FW_BOARD_ARG)
	$(call log,build,Building $(FW_DIR))
	@cmake --build $(FW_DIR) -j$(NUM_JOBS)

# Expand templates for all app/platform combinations
$(foreach app,$(APP_REGISTRY),\
  $(foreach plat,$(APP_$(app)_PLATFORMS),\
    $(if $(filter posix,$(APP_$(app)_$(plat)_TYPE)),\
      $(eval $(call _RELEASE_POSIX_template,$(app),$(plat))),\
      $(if $(APP_$(app)_$(plat)_BOARD),\
        $(eval $(call _RELEASE_FIRMWARE_BOARD_template,$(app),$(plat))),\
        $(eval $(call _RELEASE_FIRMWARE_template,$(app),$(plat)))))))

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
	@size=$$(du -h "$(RELEASE_DIR)/$(APP).tar.gz" | cut -f1); \
	  printf '$(TERM_GREEN)[%s]$(TERM_RESET) %s\n' release "$(APP).tar.gz ($$size)"
else
release: prep
	$(call _build,native release,$(HOST_RELEASE_PRESET),$(HOST_RELEASE_DIR))
endif

# ------------------------------------------------------------------------------
# release-tools: Build every tool into the native release tree
#
# A plain build carries the build-role tools only; the ops and analysis tools
# and the Python wheels build on request. This is that request for the native
# release: it builds every role into the tree `make release` builds, which is
# the tree the release builder ships and `make artifact-tools` packages. The
# directory comes from the release target's own setting, so the two cannot
# name different trees.
# ------------------------------------------------------------------------------

release-tools: prep
	@test -f "$(HOST_RELEASE_DIR)/CMakeCache.txt" || cmake --preset $(HOST_RELEASE_PRESET) $(CMAKE_VERBOSE_FLAG)
	@$(MAKE) --no-print-directory tools BUILD_DIR="$(HOST_RELEASE_DIR)"

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
.PHONY: release release-tools release-all release-clean

endif  # RELEASE_MK_GUARD
