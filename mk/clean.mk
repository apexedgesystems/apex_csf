# ==============================================================================
# mk/clean.mk - Artifact cleanup
#
# Provides targets for cleaning build artifacts, UPX outputs, documentation,
# and coverage data across all build directories.
# ==============================================================================

ifndef CLEAN_MK_GUARD
CLEAN_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Note: Clean targets operate on ALL build directories to support
# multi-platform workflows (native, jetson, rpi, stm32, etc.)

# Python tools directory
PY_TOOLS_DIR := tools

# ------------------------------------------------------------------------------
# C++ Clean Targets
# ------------------------------------------------------------------------------

# Ninja clean across all build directories
clean-ninja:
	$(call log,clean,Running ninja clean in all build directories)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'printf "  -> %s\n" "{}" && cd "{}" && ninja clean 2>/dev/null || true' \; ; \
	else \
	  printf '  -> build/ not found; skipping\n'; \
	fi

# Remove UPX-compressed outputs across all builds
clean-upx:
	$(call log,clean,Removing UPX artifacts)
	@if [ -d build ]; then \
	  find build -type f \( -name '*.upx' -o -name '*.so.upx' \) -delete 2>/dev/null || true; \
	fi

# Remove generated docs across all builds
clean-docs:
	$(call log,clean,Removing generated documentation)
	@if [ -d build ]; then \
	  find build -mindepth 2 -maxdepth 2 -name docs -type d -exec rm -rf {} + 2>/dev/null || true; \
	fi

# ------------------------------------------------------------------------------
# Tool Clean Targets
# ------------------------------------------------------------------------------
# bin/tools/<role> holds every language's tools of a role; each language
# installs its own through a stamp under <build>/*-stamps. A language's
# clean removes what that language produced together with its stamps, so
# the next build regenerates and re-installs it and the other languages'
# installed tools stay. clean-tools-build removes the role output dirs and
# every producer's stamp together, so nothing survives without its producer.

# Clean Python build artifacts from source directory
clean-py-src:
	$(call log,clean,Removing Python source artifacts)
	@find "$(PY_TOOLS_DIR)" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	@rm -rf "$(PY_TOOLS_DIR)/.pytest_cache" 2>/dev/null || true
	@rm -rf "$(PY_TOOLS_DIR)/dist" 2>/dev/null || true
	@rm -rf "$(PY_TOOLS_DIR)"/*.egg-info 2>/dev/null || true

# Clean Python build artifacts (the installed libraries, the wheels and the
# python stamps) from every build directory
clean-py-build:
	$(call log,clean,Removing Python build artifacts)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'rm -rf "{}/lib/python" "{}/tools-wheels" "{}"/*-stamps/py-*tools.built 2>/dev/null || true' \; ; \
	fi

# Clean all Python artifacts
clean-py: clean-py-src clean-py-build
	$(call log,clean,Python artifacts cleaned)

# Clean Rust build artifacts (the per-role cargo target dirs and the rust
# stamps) from every build directory
clean-rust-build:
	$(call log,clean,Removing Rust build artifacts)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'rm -rf "{}"/*-rust-target "{}"/*-stamps/rust-*tools.built 2>/dev/null || true' \; ; \
	fi

# Clean local Rust target (from development builds within tools/)
clean-rust-local:
	$(call log,clean,Removing local Rust target)
	@rm -rf tools/target 2>/dev/null || true

# Clean all Rust artifacts
clean-rust: clean-rust-build clean-rust-local
	$(call log,clean,Rust artifacts cleaned)

# Clean the shell stamps from every build directory
clean-sh-build:
	$(call log,clean,Removing shell tool stamps)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'rm -rf "{}"/*-stamps/sh-*tools.built 2>/dev/null || true' \; ; \
	fi

# Clean every installed tool and every tool producer's state
clean-tools-build: clean-py-build clean-rust-build clean-sh-build
	$(call log,clean,Removing the installed tools)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'rm -rf "{}/bin/tools" 2>/dev/null || true' \; ; \
	fi

# Clean all tool artifacts
clean-tools: clean-tools-build clean-rust-local clean-py-src
	$(call log,clean,Tool artifacts cleaned)

# ------------------------------------------------------------------------------
# Aggregate Clean Targets
# ------------------------------------------------------------------------------

# Main clean target
clean: clean-ninja clean-upx clean-docs clean-tools coverage-clean
	$(call log,clean,Done)

# Refuse distclean when non-default build dirs are present unless SURE=1.
# Runs as a prerequisite so rust/py cleanups don't fire until the guard passes.
distclean-guard:
	@if [ -d build ]; then \
	  non_default=$$(ls build 2>/dev/null | grep -vE '^hosted-' || true); \
	  if [ -n "$$non_default" ] && [ "$(SURE)" != "1" ]; then \
	    printf '$(TERM_RED)[distclean]$(TERM_RESET) cross-build dirs present:\n'; \
	    printf '%s\n' $$non_default | sed 's|^|  build/|'; \
	    printf 'Refusing without SURE=1. Run: make distclean SURE=1\n'; \
	    exit 1; \
	  fi; \
	fi

# Deep clean - remove entire build directory.
distclean: distclean-guard clean-py-src clean-rust-local
	$(call log,clean,Removing build/ and compile_commands.json)
	@rm -rf build/ compile_commands.json

# ------------------------------------------------------------------------------
# Per-Platform Clean (generated from the platform registry, mk/platforms.mk)
# ------------------------------------------------------------------------------
# clean-<platform> removes that platform's release + debug build trees -- handy
# for reclaiming disk without a full distclean. Host (cpu/cuda) build dirs are
# covered by clean/distclean; skeletons have no build tree.
_CLEAN_PLATFORMS := $(filter-out cpu cuda,$(PLAT_BUILDERS))

define _clean_platform_target
.PHONY: clean-$(1)
clean-$(1):
	$$(call log,clean,Removing $(1) build trees)
	@rm -rf build/$(P_$(1)_PRESET) $(if $(P_$(1)_DEBUG),build/$(P_$(1)_DEBUG))
endef
$(foreach p,$(_CLEAN_PLATFORMS),$(eval $(call _clean_platform_target,$(p))))

.PHONY: clean-cross
clean-cross: $(foreach p,$(_CLEAN_PLATFORMS),clean-$(p))
	$(call log,clean,All cross/firmware build trees removed)

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: clean clean-ninja clean-upx clean-docs distclean distclean-guard
.PHONY: clean-py clean-py-src clean-py-build
.PHONY: clean-rust clean-rust-build clean-rust-local
.PHONY: clean-sh-build clean-tools-build clean-tools

endif  # CLEAN_MK_GUARD