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
# A demo's checkout scripts keep their tests beside them, in the demo's
# test/ directory; the gate collects those directories with the tools' own.
PY_DEMO_TEST_DIRS := demos/apex_action_demo/test demos/apex_horizon_demo/rover_mcu/test
test-py:
	$(call log,test,Running Python tools tests)
	@cd "$(PY_TOOLS_DIR)" && uv sync --frozen --no-editable && \
	  (uv run --no-sync pytest -v tests $(addprefix $(CURDIR)/,$(PY_DEMO_TEST_DIRS)) || test $$? -eq 5)

# Rust tools unit tests
test-rust:
	$(call log,test,Running Rust tools tests)
	@cd tools && cargo test --workspace

# Shell tools smoke: exercises ops_sdk_package.sh end to end against a
# synthetic build tree (a stub struct dictionary) and asserts the SDK
# tarball's shape. Host-runnable in seconds -- bash and tar only -- so the
# shell family has an executed check, not just shellcheck's static one.
test-sh:
	$(call log,test,Running shell tools smoke)
	@tmp=$$(mktemp -d) && trap 'rm -rf "$$tmp"' EXIT && \
	  mkdir -p "$$tmp/build/apex_data_db" "$$tmp/build/tprm_templates" && \
	  printf '{"structs":{"SmokeParams":{"category":"TUNABLE_PARAM","fields":[]}}}\n' \
	    > "$$tmp/build/apex_data_db/SmokeComponent.json" && \
	  printf '[SmokeParams]\n' > "$$tmp/build/tprm_templates/SmokeComponent_SmokeParams.toml" && \
	  bash tools/sh/bin/ops_sdk_package.sh --app SmokeApp --build-dir "$$tmp/build" && \
	  tarball="$$tmp/build/ops_sdk/SmokeApp-ops-sdk.tar.gz" && \
	  test -f "$$tarball" && \
	  tar -tzf "$$tarball" | grep -q "SmokeApp/structs/SmokeComponent.json" && \
	  tar -tzf "$$tarball" | grep -q "SmokeApp/templates/SmokeComponent_SmokeParams.toml" && \
	  tar -tzf "$$tarball" | grep -q "SmokeApp/README.md"
	$(call log_ok,test,shell smoke: SDK tarball shape verified)

# ------------------------------------------------------------------------------
# Conformance contracts (compat/)
# ------------------------------------------------------------------------------
# One command per contract, running every in-repo suite that pins it; the
# gate runs the same suites via the ci classifier when a contract or one of
# its sides changes. Requires a debug build for the C++ side.

COMPAT_CONTRACTS := tprm

compat: $(addprefix compat-,$(COMPAT_CONTRACTS))

compat-tprm:
	$(call log,compat,Verifying the TPRM contract (rust + C++))
	@cd tools && cargo test --locked -p apex-tprm --test golden_vectors
	@GTEST_FILTER='GoldenVectors.*' \
	  "$(BUILD_DIR)/bin/utests/TestSystemCoreSystemComponent"
	$(call log_ok,compat,TPRM contract green on both sides)

.PHONY: compat compat-tprm

# ------------------------------------------------------------------------------
# cdef: spec-generated .auto headers (regen + drift gate)
# ------------------------------------------------------------------------------
# Every apex_data.toml with a [[fields.<Struct>]] spec owns committed
# .auto headers generated from it. `make cdef` regenerates in place;
# `make check-cdef` regenerates to a scratch tree and diffs, so a
# hand-edited generated file (or a stale one after a spec change)
# cannot merge. Requires the rust tools (cdef_gen).

# Spec-defined layouts come from inline [[fields]] arrays or a
# referenced proto_spec file; either marks the manifest as owning
# committed .auto artifacts.
CDEF_MANIFESTS := $(shell grep -rlE "^(\[\[fields\.|proto_spec)" --include="*_data.toml" --include="apex_data.toml" demos src apps 2>/dev/null)

cdef: tools-build
	$(call log,cdef,Regenerating .auto headers from component specs)
	@for m in $(CDEF_MANIFESTS); do \
	  "$(BUILD_DIR)/bin/tools/build/cdef_gen" --manifest "$$m" || exit 1; \
	done
	$(call log_ok,cdef,.auto headers regenerated)

# A directory may hold several manifests emitting into one .auto tree
# (e.g. hil model: plant + vfc), so regeneration and diffing group by
# directory -- every manifest of a directory regenerates into the same
# scratch before the whole tree is compared.
check-cdef: tools-build
	$(call log,check-cdef,Diffing committed .auto headers against their specs)
	@fail=0; for dir in $$(for m in $(CDEF_MANIFESTS); do dirname "$$m"; done | sort -u); do \
	  scratch=$$(mktemp -d); \
	  for m in $(CDEF_MANIFESTS); do \
	    [ "$$(dirname "$$m")" = "$$dir" ] || continue; \
	    "$(BUILD_DIR)/bin/tools/build/cdef_gen" --manifest "$$m" --output "$$scratch" > /dev/null || exit 1; \
	  done; \
	  if ! diff -ru "$$dir/.auto" "$$scratch" > /dev/null 2>&1; then \
	    printf '[check-cdef] DRIFT: %s/.auto does not match its specs\n' "$$dir"; \
	    diff -ru "$$dir/.auto" "$$scratch" | head -20; \
	    fail=1; \
	  fi; \
	  rm -rf "$$scratch"; \
	done; \
	[ $$fail -eq 0 ] || exit 1
	$(call log_ok,check-cdef,.auto headers match their specs)

# Every registered data manifest must produce its dictionary with no
# entry left behind a `_warning` key, because ground tooling regenerates
# its target directories from these. The inventory is the one CMake
# writes at configure from the apex_add_data_manifest registrations (the
# same list apex-data-db consumes); the tools-build prerequisite
# configures the tree, so the list is present. Generation goes to a
# scratch tree so the gate is read-only; the first failing manifest
# fails it.
check-data-db: tools-build
	$(call log,check-data-db,Generating every registered dictionary to a scratch tree)
	@list="$(BUILD_DIR)/apex_data_manifests.txt"; \
	[ -s "$$list" ] || { printf '[check-data-db] no registered manifests in %s\n' "$$list"; exit 1; }; \
	scratch=$$(mktemp -d) || exit 1; fail=0; count=0; \
	while IFS= read -r m; do \
	  [ -z "$$m" ] && continue; \
	  count=$$((count + 1)); \
	  "$(BUILD_DIR)/bin/tools/build/apex_data_gen" --manifest "$$m" --output "$$scratch" > /dev/null || { fail=1; break; }; \
	done < "$$list"; \
	if [ $$fail -eq 0 ] && grep -l '"_warning"' "$$scratch"/*.json; then \
	  printf '[check-data-db] a dictionary above carries a _warning entry\n'; fail=1; \
	fi; \
	rm -rf "$$scratch"; \
	[ $$fail -eq 0 ] || exit 1; \
	printf '[check-data-db] %s registered dictionaries generate clean\n' "$$count"

# Contract checks for the pieces check-data-db stands on, so that removing
# the helper's explicit-path error or the gate's failure propagation cannot
# pass CI on valid inputs alone.
#
# check-data-manifest-helper configures cmake/apex/test/data_manifest once
# per case and asserts status, the registered list and the diagnostic.
# check-data-db-contract hands the real check-data-db recipe a scratch
# BUILD_DIR holding the built generator and a one-entry registered list per
# fixture under mk/fixtures/check-data-db, asserting the gate's verdict.
HELPER_FIXTURE := cmake/apex/test/data_manifest
GATE_FIXTURES := mk/fixtures/check-data-db

check-data-manifest-helper:
	$(call log,check-data-manifest-helper,Configuring the apex_add_data_manifest fixture per case)
	@scratch=$$(mktemp -d) || exit 1; fail=0; \
	run() { cmake -S "$(HELPER_FIXTURE)" -B "$$scratch/$$1" -DCASE="$$1" > "$$scratch/$$1.log" 2>&1; echo $$?; }; \
	expect() { grep -q -- "$$2" "$$scratch/$$1.log" || { printf '[check-data-manifest-helper] %s: missing diagnostic "%s"\n' "$$1" "$$2"; fail=1; }; }; \
	[ "$$(run default-absent)" -eq 0 ] && [ ! -s "$$scratch/default-absent/apex_data_manifests.txt" ] \
	  || { printf '[check-data-manifest-helper] default-absent: expected success with an empty list\n'; fail=1; }; \
	[ "$$(run valid)" -eq 0 ] && grep -q 'present_data.toml$$' "$$scratch/valid/apex_data_manifests.txt" \
	  || { printf '[check-data-manifest-helper] valid: expected success listing present_data.toml\n'; fail=1; }; \
	[ "$$(run explicit-missing)" -ne 0 ] || { printf '[check-data-manifest-helper] explicit-missing: expected a configure error\n'; fail=1; }; \
	expect explicit-missing 'MANIFEST path does not exist'; expect explicit-missing 'missing_data.toml'; \
	[ "$$(run notfound)" -ne 0 ] || { printf '[check-data-manifest-helper] notfound: expected a configure error\n'; fail=1; }; \
	expect notfound 'MANIFEST path does not exist'; expect notfound 'NOTFOUND'; \
	[ "$$(run keyword-without-value)" -ne 0 ] || { printf '[check-data-manifest-helper] keyword-without-value: expected a configure error\n'; fail=1; }; \
	expect keyword-without-value 'use no arguments or MANIFEST <path>'; \
	[ "$$(run stray-argument)" -ne 0 ] || { printf '[check-data-manifest-helper] stray-argument: expected a configure error\n'; fail=1; }; \
	expect stray-argument 'use no arguments or MANIFEST <path>'; expect stray-argument 'OFF'; \
	rm -rf "$$scratch"; \
	[ $$fail -eq 0 ] || exit 1
	$(call log_ok,check-data-manifest-helper,default no-op; valid listed; missing and -NOTFOUND paths and bare or stray arguments fail configure)

check-data-db-contract: tools-build
	$(call log,check-data-db-contract,Driving check-data-db with one fixture per verdict)
	@scratch=$$(mktemp -d) || exit 1; fail=0; \
	mkdir -p "$$scratch/bin/tools/build" && ln -s "$(abspath $(BUILD_DIR))/bin/tools/build/apex_data_gen" "$$scratch/bin/tools/build/apex_data_gen"; \
	run() { printf '%s\n' "$(abspath $(GATE_FIXTURES))/$$1/apex_data.toml" > "$$scratch/apex_data_manifests.txt"; \
	        $(MAKE) --no-print-directory -o tools-build check-data-db BUILD_DIR="$$scratch" > "$$scratch/$$1.log" 2>&1; echo $$?; }; \
	expect() { grep -q -- "$$2" "$$scratch/$$1.log" || { printf '[check-data-db-contract] %s: missing "%s"\n' "$$1" "$$2"; fail=1; }; }; \
	[ "$$(run valid)" -eq 0 ] || { printf '[check-data-db-contract] valid: expected the gate to pass\n'; fail=1; }; \
	expect valid '1 registered dictionaries generate clean'; \
	[ "$$(run broken-header)" -ne 0 ] || { printf '[check-data-db-contract] broken-header: expected the gate to fail\n'; fail=1; }; \
	expect broken-header 'declares header'; \
	[ "$$(run warning)" -ne 0 ] || { printf '[check-data-db-contract] warning: expected the gate to fail\n'; fail=1; }; \
	expect warning 'carries a _warning entry'; \
	rm -rf "$$scratch"; \
	[ $$fail -eq 0 ] || exit 1
	$(call log_ok,check-data-db-contract,valid passes; a declared-header error and a _warning entry fail the gate)

.PHONY: cdef check-cdef check-data-db check-data-manifest-helper check-data-db-contract

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
	@cd tools && cargo llvm-cov --workspace --no-report && \
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

.PHONY: test testp testp-cpu testp-cuda test-py test-rust test-sh coverage-py coverage-rust smoke

endif  # TEST_MK_GUARD