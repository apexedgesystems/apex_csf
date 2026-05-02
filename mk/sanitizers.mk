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

ASAN_PRESET    ?= native-linux-asan
TSAN_PRESET    ?= native-linux-tsan
UBSAN_PRESET   ?= native-linux-ubsan

ASAN_DIR       := build/$(ASAN_PRESET)
TSAN_DIR       := build/$(TSAN_PRESET)
UBSAN_DIR      := build/$(UBSAN_PRESET)

# ASan + CUDA workaround: ASan's shadow memory reservation collides with
# CUDA's virtual address space. protect_shadow_gap=0 disables the
# protection that triggers the conflict. detect_leaks=0 turns off LSan,
# whose noise against CUDA's persistent allocations is not actionable.
ASAN_RUN_OPTIONS ?= protect_shadow_gap=0:detect_leaks=0

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# _sanitizer_run: configure, build, and test with a sanitizer preset
# Usage: $(call _sanitizer_run,tag,display_name,preset,build_dir,extra_env)
# extra_env is prefixed verbatim onto the ctest invocation (e.g. ASAN_OPTIONS=...).
define _sanitizer_run
	$(call _build,$(2),$(3),$(4))
	$(call log,$(1),Running tests)
	@cd "$(4)" && $(5) $(call with_lib_path,ctest --output-on-failure)
endef

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

# AddressSanitizer - detects memory errors (use-after-free, buffer overflow)
asan: prep
	$(call _sanitizer_run,asan,AddressSanitizer,$(ASAN_PRESET),$(ASAN_DIR),ASAN_OPTIONS="$(ASAN_RUN_OPTIONS)")

# ThreadSanitizer - detects data races
tsan: prep
	$(call _sanitizer_run,tsan,ThreadSanitizer,$(TSAN_PRESET),$(TSAN_DIR),)

# UndefinedBehaviorSanitizer - detects undefined behavior
ubsan: prep
	$(call _sanitizer_run,ubsan,UBSanitizer,$(UBSAN_PRESET),$(UBSAN_DIR),)

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: asan tsan ubsan

endif  # SANITIZERS_MK_GUARD
