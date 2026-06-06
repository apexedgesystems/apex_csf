# ==============================================================================
# mk/coverage.mk - Code coverage instrumentation and reporting
#
# LLVM source-based coverage using Clang. Generates per-library HTML reports,
# text summaries, and LCOV exports for CI integration.
#
# Coverage builds get their own CMake preset and build directory so they
# don't invalidate the normal `make debug` build.
# ==============================================================================

ifndef COVERAGE_MK_GUARD
COVERAGE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Note: Coverage is native-only. Cross-compiled and bare-metal targets
# don't support host-side coverage instrumentation.

COVERAGE_PRESET ?= hosted-x86_64-coverage
COVERAGE_DIR    := build/$(COVERAGE_PRESET)

# Minimum aggregate coverage required by `make coverage-check`. Floors sit just
# below the currently-observed numbers so the gate guards against regressions
# without firing spuriously -- ratchet up over time, never down.
#
# These reflect the per-unit-test measure (each library by its own unit test).
# The earlier 60/50 were aspirational and never actually enforced -- coverage
# reported n/a on clean builds until the bin/utests path fix, so the gate always
# passed. Converting to the union-of-all-unit-tests measure will raise these
# (tracked in the coverage-consolidation ticket).
COVERAGE_MIN_LINE   ?= 28
COVERAGE_MIN_REGION ?= 45

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

coverage: prep
	$(call _build,Coverage,$(COVERAGE_PRESET),$(COVERAGE_DIR))
	$(call log,coverage,Running coverage tests)
	@ctest --preset $(COVERAGE_PRESET) -L Coverage -j1 || true
	$(call log,coverage,Generating reports)
	@cmake --build "$(COVERAGE_DIR)" --target coverage-report

# Same as `coverage` but fails if aggregate region/line % drops below floor.
coverage-check: coverage
	$(call log,coverage-check,Checking thresholds (region >= $(COVERAGE_MIN_REGION)% / line >= $(COVERAGE_MIN_LINE)%))
	@status="$(COVERAGE_DIR)/coverage/.coverage-status"; \
	  if [ ! -f "$$status" ]; then \
	    printf '$(TERM_RED)[coverage-check]$(TERM_RESET) status file not found: %s\n' "$$status"; \
	    exit 1; \
	  fi; \
	  region=$$(awk -F= '/^REGION_COVERAGE=/{print $$2}' "$$status"); \
	  line=$$(awk -F= '/^LINE_COVERAGE=/{print $$2}' "$$status"); \
	  libs=$$(awk -F= '/^LIB_COUNT=/{print $$2}' "$$status"); \
	  if [ -z "$$region" ] || [ -z "$$line" ]; then \
	    printf '$(TERM_RED)[coverage-check]$(TERM_RESET) status file is malformed:\n%s\n' "$$(cat "$$status")"; \
	    exit 1; \
	  fi; \
	  bad=0; \
	  awk "BEGIN {exit !($$region >= $(COVERAGE_MIN_REGION))}" || { \
	    printf '$(TERM_RED)[coverage-check]$(TERM_RESET) region %s%% below floor %s%%\n' "$$region" "$(COVERAGE_MIN_REGION)"; \
	    bad=1; \
	  }; \
	  awk "BEGIN {exit !($$line >= $(COVERAGE_MIN_LINE))}" || { \
	    printf '$(TERM_RED)[coverage-check]$(TERM_RESET) line %s%% below floor %s%%\n' "$$line" "$(COVERAGE_MIN_LINE)"; \
	    bad=1; \
	  }; \
	  if [ "$$bad" -ne 0 ]; then exit 1; fi; \
	  printf '[coverage-check] OK -- %s libs, %s%% regions, %s%% lines\n' "$$libs" "$$region" "$$line"

coverage-clean:
	$(call log,coverage,Cleaning coverage artifacts)
	@cmake --build "$(COVERAGE_DIR)" --target coverage-clean 2>/dev/null || true
	@find "$(COVERAGE_DIR)" -name '*.profraw' -delete 2>/dev/null || true

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: coverage coverage-check coverage-clean

endif  # COVERAGE_MK_GUARD
