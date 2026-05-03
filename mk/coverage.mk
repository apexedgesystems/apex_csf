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

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

coverage: prep
	$(call _build,Coverage,$(COVERAGE_PRESET),$(COVERAGE_DIR))
	$(call log,coverage,Running coverage tests)
	@cd "$(COVERAGE_DIR)" && $(call with_lib_path,ctest -L Coverage -j1 --no-tests=ignore --output-on-failure) || true
	$(call log,coverage,Generating reports)
	@cmake --build "$(COVERAGE_DIR)" --target coverage-report

coverage-clean:
	$(call log,coverage,Cleaning coverage artifacts)
	@cmake --build "$(COVERAGE_DIR)" --target coverage-clean 2>/dev/null || true
	@find "$(COVERAGE_DIR)" -name '*.profraw' -delete 2>/dev/null || true

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: coverage coverage-clean

endif  # COVERAGE_MK_GUARD
