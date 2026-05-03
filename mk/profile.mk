# ==============================================================================
# mk/profile.mk - Build observability
#
# Wraps cmake's --profiling-format and ninjatracing to expose chrome:tracing
# JSON traces of configure-time and build-time work. Open the resulting
# .json files at chrome://tracing or https://ui.perfetto.dev.
#
# Usage:
#   make profile-configure    Configure-time hotspots (cmake call graph)
#   make profile-build        Build-time hotspots (ninja edge timings)
# ==============================================================================

ifndef PROFILE_MK_GUARD
PROFILE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Uses HOST_DEBUG_PRESET / HOST_DEBUG_DIR from Makefile.

PROFILE_DIR ?= $(HOST_DEBUG_DIR)/profiles

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

profile-configure: prep
	@mkdir -p "$(PROFILE_DIR)"
	$(call log,profile,Configure-time profile -> $(PROFILE_DIR)/configure.json)
	@cmake --preset $(HOST_DEBUG_PRESET) $(CMAKE_VERBOSE_FLAG) $(CMAKE_EXTRA_ARGS) \
	  --profiling-format=google-trace \
	  --profiling-output="$(PROFILE_DIR)/configure.json"
	$(call log_ok,profile,Open in chrome://tracing or https://ui.perfetto.dev)

profile-build: prep
	@test -f "$(HOST_DEBUG_DIR)/.ninja_log" || \
	  { printf '$(TERM_RED)[profile]$(TERM_RESET) %s not found. Run `make debug` first.\n' \
	    "$(HOST_DEBUG_DIR)/.ninja_log"; exit 1; }
	@mkdir -p "$(PROFILE_DIR)"
	$(call log,profile,Build-time profile -> $(PROFILE_DIR)/build.json)
	@ninjatracing "$(HOST_DEBUG_DIR)/.ninja_log" > "$(PROFILE_DIR)/build.json"
	$(call log_ok,profile,Open in chrome://tracing or https://ui.perfetto.dev)

profile-clean:
	$(call log,profile,Removing $(PROFILE_DIR))
	@rm -rf "$(PROFILE_DIR)"

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: profile-configure profile-build profile-clean

endif  # PROFILE_MK_GUARD
