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
PY_TOOLS_DIR := tools/py

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
# Python Clean Targets
# ------------------------------------------------------------------------------

# Clean Python build artifacts from source directory
clean-py-src:
	$(call log,clean,Removing Python source artifacts)
	@find "$(PY_TOOLS_DIR)" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	@rm -rf "$(PY_TOOLS_DIR)/.pytest_cache" 2>/dev/null || true
	@rm -rf "$(PY_TOOLS_DIR)/dist" 2>/dev/null || true
	@rm -rf "$(PY_TOOLS_DIR)"/*.egg-info 2>/dev/null || true

# Clean Python build artifacts from build directory
clean-py-build:
	$(call log,clean,Removing Python build artifacts)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'rm -rf "{}/lib/python" "{}/bin/tools/py" "{}/wheels" 2>/dev/null || true' \; ; \
	fi

# Clean all Python artifacts
clean-py: clean-py-src clean-py-build
	$(call log,clean,Python artifacts cleaned)

# ------------------------------------------------------------------------------
# Rust Clean Targets
# ------------------------------------------------------------------------------

# Clean Rust build artifacts from build directory
clean-rust-build:
	$(call log,clean,Removing Rust build artifacts)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'rm -rf "{}/bin/tools/rust" 2>/dev/null || true' \; ; \
	fi

# Clean local Rust target (from development builds within tools/rust/)
clean-rust-local:
	$(call log,clean,Removing local Rust target)
	@rm -rf tools/rust/target 2>/dev/null || true

# Clean all Rust artifacts
clean-rust: clean-rust-build clean-rust-local
	$(call log,clean,Rust artifacts cleaned)

# ------------------------------------------------------------------------------
# Aggregate Clean Targets
# ------------------------------------------------------------------------------

# Main clean target
clean: clean-ninja clean-upx clean-docs clean-py clean-rust coverage-clean
	$(call log,clean,Done)

# Refuse distclean when non-default build dirs are present unless SURE=1.
# Runs as a prerequisite so rust/py cleanups don't fire until the guard passes.
distclean-guard:
	@if [ -d build ]; then \
	  non_default=$$(ls build 2>/dev/null | grep -vE '^native-linux-' || true); \
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
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: clean clean-ninja clean-upx clean-docs distclean distclean-guard
.PHONY: clean-py clean-py-src clean-py-build
.PHONY: clean-rust clean-rust-build clean-rust-local

endif  # CLEAN_MK_GUARD