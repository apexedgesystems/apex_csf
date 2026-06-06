# ==============================================================================
# mk/sanitizers.mk - Runtime sanitizer builds
#
# AddressSanitizer (ASan), ThreadSanitizer (TSan), and UndefinedBehaviorSanitizer
# (UBSan) for detecting memory errors, data races, and undefined behavior.
#
# Each sanitizer has its own CMake preset and build directory, so a normal
# `make debug` build is not invalidated by `make asan` (or vice versa).
# ==============================================================================

ifndef SANITIZERS_MK_GUARD
SANITIZERS_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Note: Sanitizers are native-only. Cross-compiled and bare-metal targets
# don't support runtime sanitizer instrumentation.

ASAN_PRESET       ?= hosted-x86_64-asan
TSAN_PRESET       ?= hosted-x86_64-tsan
UBSAN_PRESET      ?= hosted-x86_64-ubsan
ASAN_UBSAN_PRESET ?= hosted-x86_64-asan-ubsan

ASAN_DIR       := build/$(ASAN_PRESET)
TSAN_DIR       := build/$(TSAN_PRESET)
UBSAN_DIR      := build/$(UBSAN_PRESET)
ASAN_UBSAN_DIR := build/$(ASAN_UBSAN_PRESET)

# Test-execution environment (LD_LIBRARY_PATH, and ASAN_OPTIONS for asan) lives
# in the CMake testPresets so `ctest --preset hosted-x86_64-asan` is
# self-sufficient. The asan preset sets ASAN_OPTIONS=protect_shadow_gap=0
# (avoids the ASan/CUDA virtual-memory conflict) :detect_leaks=0 (disables
# LSan, noisy against CUDA's persistent allocations).

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# _sanitizer_run: configure, build, and test with a sanitizer preset.
# Usage: $(call _sanitizer_run,tag,display_name,preset,build_dir)
# Test env comes from the CMake testPreset of the same name.
define _sanitizer_run
	$(call _build,$(2),$(3),$(4))
	$(call log,$(1),Running tests)
	@ctest --preset $(3)
endef

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

# AddressSanitizer - detects memory errors (use-after-free, buffer overflow)
asan: prep
	$(call _sanitizer_run,asan,AddressSanitizer,$(ASAN_PRESET),$(ASAN_DIR))

# ThreadSanitizer - detects data races
tsan: prep
	$(call _sanitizer_run,tsan,ThreadSanitizer,$(TSAN_PRESET),$(TSAN_DIR))

# UndefinedBehaviorSanitizer - detects undefined behavior
ubsan: prep
	$(call _sanitizer_run,ubsan,UBSanitizer,$(UBSAN_PRESET),$(UBSAN_DIR))

# Address + UndefinedBehavior in one build (compatible sanitizers). This is the
# combination the CI gate runs: ASan catches memory errors, UBSan rides along.
asan-ubsan: prep
	$(call _sanitizer_run,asan+ubsan,Address+UBSanitizer,$(ASAN_UBSAN_PRESET),$(ASAN_UBSAN_DIR))

# ------------------------------------------------------------------------------
# Hardened build
# ------------------------------------------------------------------------------
# Release build with defense-in-depth mitigations (FORTIFY_SOURCE=3, libstdc++
# assertions, stack/CF protection, auto-var-init). Builds and runs the full
# suite so the assertions and fortified calls exercise real paths.

HARDENED_PRESET ?= hosted-x86_64-hardened
HARDENED_DIR    := build/$(HARDENED_PRESET)

hardened: prep
	$(call _sanitizer_run,hardened,Hardened,$(HARDENED_PRESET),$(HARDENED_DIR))

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: asan tsan ubsan asan-ubsan hardened

endif  # SANITIZERS_MK_GUARD
