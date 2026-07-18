# ==============================================================================
# mk/test.mk - Test execution
#
# CTest wrappers for unit tests and timing-sensitive tests.
# Supports both serial and parallel execution modes with TTY output and logging.
#
# Note: Performance tests (ptst/) are not in CTest. Run directly:
#   ./build/hosted-x86_64-debug/bin/ptests/LibraryName_PTEST
# ==============================================================================

ifndef TEST_MK_GUARD
TEST_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Note: Tests run on native host builds only. Cross-compiled targets (Jetson,
# RPi) and bare-metal targets (STM32) require on-device execution.

# Uses BUILD_DIR and NUM_JOBS from common.mk

# Test labels (must match CMake test properties)
COVERAGE_LABEL ?= Coverage
TIMING_LABEL   ?= Timing
CUDA_LABEL     ?= cuda

# Log files
TEST_LOG ?= ctest.log

# Python tools directory
PY_TOOLS_DIR := tools/py
PY_LIB_DIR   := $(BUILD_DIR)/lib/python

# ------------------------------------------------------------------------------
# CTest Command Presets
# ------------------------------------------------------------------------------
# Test-execution config (LD_LIBRARY_PATH, output-on-failure, no-tests-ignore)
# lives in the CMake testPresets; Make selects only the label subset and the
# serial/parallel strategy. TEST_PRESET is the testPreset name (== build-dir
# basename, which equals the preset name by convention).

TEST_PRESET ?= $(notdir $(BUILD_DIR))
LOG_FILE    := $(BUILD_DIR)/$(TEST_LOG)

# CUDA-labeled tests link the NVIDIA driver and only load where a GPU is
# present, so the default run drops them on a driverless host (an explicit
# testp-cuda still selects them). HAVE_GPU is non-empty when a device is
# visible; override it (make testp HAVE_GPU=) to force the CPU-only set.
HAVE_GPU := $(shell nvidia-smi -L >/dev/null 2>&1 && echo 1)
CUDA_LE  := $(if $(HAVE_GPU),,|$(CUDA_LABEL))

# All tests except Coverage (and CUDA where no GPU), serial execution
# Note: Perf tests are not in CTest (use bin/ptests/* directly)
CTEST_ALL_SERIAL := ctest --preset $(TEST_PRESET) -LE "$(COVERAGE_LABEL)$(CUDA_LE)" -j1

# All tests except Coverage and Timing (and CUDA where no GPU), parallel
CTEST_ALL_PARALLEL := ctest --preset $(TEST_PRESET) -LE "$(COVERAGE_LABEL)|$(TIMING_LABEL)$(CUDA_LE)" -j$(NUM_JOBS)

# Timing tests only, serial execution
CTEST_TIMING_SERIAL := ctest --preset $(TEST_PRESET) -L "$(TIMING_LABEL)" -j1

# CUDA-labeled tests only, parallel execution (needs a GPU at runtime)
CTEST_CUDA_PARALLEL := ctest --preset $(TEST_PRESET) -L "$(CUDA_LABEL)" -LE "$(COVERAGE_LABEL)" -j$(NUM_JOBS)

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# Log section header (outputs to stdout, caller handles tee)
# Usage: $(call _test_header,title)
define _test_header
printf '%s\nctest: %s\ndir: %s\n%s\n' "============================================================" "$(1)" "$(BUILD_DIR)" "------------------------------------------------------------"
endef

# Log section footer (outputs to stdout, caller handles tee)
# Usage: $(call _test_footer,summary,logfile)
define _test_footer
printf '%s\ndone: %s\nlog: %s\n%s\n' "------------------------------------------------------------" "$(1)" "$(2)" "============================================================"
endef

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

# All tests (serial) - excludes Coverage and Perf labels
test: debug
	$(call log,test,Running all tests (serial))
	@: > "$(LOG_FILE)"
	@$(call _test_header,ALL (serial) - excluding: $(COVERAGE_LABEL)) | tee -a "$(LOG_FILE)"
	@bash -o pipefail -c '$(CTEST_ALL_SERIAL) 2>&1 | tee -a "$(LOG_FILE)"'
	@$(call _test_footer,all (serial),$(LOG_FILE)) | tee -a "$(LOG_FILE)"

# Parallel tests - runs non-timing parallel, then timing serial
testp: debug
	$(call log,test,Running tests (parallel + timing serial))
	@: > "$(LOG_FILE)"
	@$(call _test_header,NON-TIMING (parallel -j$(NUM_JOBS))) | tee -a "$(LOG_FILE)"
	@bash -o pipefail -c '$(CTEST_ALL_PARALLEL) 2>&1 | tee -a "$(LOG_FILE)"'
	@printf '\n' | tee -a "$(LOG_FILE)"
	@$(call _test_header,TIMING (serial)) | tee -a "$(LOG_FILE)"
	@bash -o pipefail -c '$(CTEST_TIMING_SERIAL) 2>&1 | tee -a "$(LOG_FILE)"'
	@$(call _test_footer,parallel + timing,$(LOG_FILE)) | tee -a "$(LOG_FILE)"

# CPU tests only - force-excludes the cuda set regardless of GPU presence
testp-cpu:
	@$(MAKE) --no-print-directory testp HAVE_GPU=

# CUDA tests only - selects the cuda-labeled set (needs a GPU at runtime)
testp-cuda: debug
	$(call log,test,Running CUDA tests (parallel))
	@: > "$(LOG_FILE)"
	@$(call _test_header,CUDA (parallel -j$(NUM_JOBS))) | tee -a "$(LOG_FILE)"
	@bash -o pipefail -c '$(CTEST_CUDA_PARALLEL) 2>&1 | tee -a "$(LOG_FILE)"'
	@$(call _test_footer,cuda (parallel),$(LOG_FILE)) | tee -a "$(LOG_FILE)"

# Python tools unit tests. uv keeps the env in-tree (.venv) and resolves
# offline in the build images from the baked uv cache (UV_OFFLINE=1).
# --no-editable: test the project installed the way the wheel ships, and skip
# the editable-install machinery (its `editables` backend never enters the
# lock, so an editable sync could not resolve offline anyway).
test-py:
	$(call log,test,Running Python tools tests)
	@cd "$(PY_TOOLS_DIR)" && uv sync --frozen --no-editable && (uv run --no-sync pytest -v || test $$? -eq 5)

# Rust tools unit tests
test-rust:
	$(call log,test,Running Rust tools tests)
	@cd tools/rust && cargo test

# Tooling coverage (nightly). Each runs its tool test suite under coverage
# instrumentation and emits a human-readable summary plus a machine report,
# so the nightly leg both gates (tests still fail the job) and measures. The
# PR gate keeps the lighter test-py/test-rust. exit 5 = pytest "no tests".
#
# Floors gate only when SCAN_GATE=1 (the nightly's policy switch, same as the
# security scanners); local runs measure without failing. Set from observed
# actuals (rust 56.4 / py 33.4, nightly 2026-07-03) with churn margin --
# ratchet up, never down.
COVERAGE_RUST_MIN_LINE ?= 50
COVERAGE_PY_MIN_LINE   ?= 30

coverage-py:
	$(call log,coverage,Measuring Python tools coverage)
	@cd "$(PY_TOOLS_DIR)" && uv sync --frozen --no-editable && \
	  (uv run --no-sync pytest --cov=apex_tools --cov-report=term-missing \
	    --cov-report=xml:coverage-py.xml \
	    $(if $(filter 1,$(SCAN_GATE)),--cov-fail-under=$(COVERAGE_PY_MIN_LINE),) \
	    || test $$? -eq 5)

coverage-rust:
	$(call log,coverage,Measuring Rust tools coverage)
	@cd tools/rust && cargo llvm-cov --no-report && \
	  cargo llvm-cov report --lcov --output-path coverage-rust.lcov && \
	  cargo llvm-cov report \
	    $(if $(filter 1,$(SCAN_GATE)),--fail-under-lines $(COVERAGE_RUST_MIN_LINE),)

# ------------------------------------------------------------------------------
# Demo smoke tests
# ------------------------------------------------------------------------------
# Run app smoke scripts (<root>/<app>/scripts/smoke.sh, demos/ or apps/) against the built
# binaries. The build system stays generic: it knows only the convention and
# passes APEX_BIN_DIR; each script owns its app-specific run and verification.
#
# CI passes APP=<name> so the gate runs ONLY the smoke it intends -- never
# "every app that happens to ship a script" (a GPU/hardware demo's smoke must
# not be pulled into the CPU gate). With no APP, runs all discovered scripts as
# a local convenience only. Reuses the debug build (no separate build).

APP_SMOKE_SCRIPTS := $(if $(APP),$(or $(firstword $(wildcard demos/$(APP)/scripts/smoke.sh apps/$(APP)/scripts/smoke.sh)),demos/$(APP)/scripts/smoke.sh),$(wildcard demos/*/scripts/smoke.sh apps/*/scripts/smoke.sh))

smoke: debug
	@for s in $(APP_SMOKE_SCRIPTS); do \
	  test -f "$$s" || { echo "smoke: no such script: $$s"; exit 1; }; \
	  $(call log,smoke,$$s); \
	  APEX_BIN_DIR="$(HOST_DEBUG_DIR)/bin" bash "$$s" || exit 1; \
	done

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: test testp testp-cpu testp-cuda test-py test-rust coverage-py coverage-rust smoke

endif  # TEST_MK_GUARD