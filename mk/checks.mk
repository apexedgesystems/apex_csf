# ==============================================================================
# mk/checks.mk - Unified registry of code-quality checks
#
# One place that knows every static / sanitizer / security / hardening check,
# its category, and which CI tier runs it. The per-check recipes live in their
# own files (sanitizers.mk, tools.mk, security.mk, ...); this file only groups
# them so you can run a category, run a tier, or list what exists.
#
# Adding a check:
#   1. define its `make <name>` target in the appropriate mk/ file
#   2. add <name> to the matching CHECKS_<CATEGORY> list below
#   3. add <name> to CHECKS_GATE and/or CHECKS_NIGHTLY if CI should run it
# The aggregate targets, discovery, and the CI matrix pick it up automatically.
#
# Run one:        make asan
# Run a category: make checks-static          (all static-analysis checks)
# Run a tier:     make checks-nightly          (everything the nightly runs)
# Discover:       make list-checks
# ==============================================================================

ifndef CHECKS_MK_GUARD
CHECKS_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Registry -- categories (each entry is a make target defined elsewhere)
# ------------------------------------------------------------------------------

CHECKS_SANITIZER := asan tsan ubsan rtsan asan-ubsan
CHECKS_STATIC    := static cppcheck
CHECKS_SECURITY  := trivy gitleaks osv semgrep
CHECKS_HARDENING := hardened
CHECKS_COVERAGE  := coverage-check

CHECKS_ALL := $(sort $(CHECKS_SANITIZER) $(CHECKS_STATIC) $(CHECKS_SECURITY) \
                     $(CHECKS_HARDENING) $(CHECKS_COVERAGE))

# ------------------------------------------------------------------------------
# Registry -- CI tiers (which checks run where)
# ------------------------------------------------------------------------------
# gate    = per-PR (kept fast; reuses the gate build where possible)
# nightly = scheduled, heavier checks with their own builds

CHECKS_GATE    := asan-ubsan static gitleaks
# test-rust/test-py ride nightly because PR CI runs them only when tools/rust
# or tools/py changed -- nightly re-proves them on main (C++ tests are already
# re-proven nightly through the coverage-check/tsan/rtsan builds).
# debug/asan-ubsan ride nightly to seed the PR gate's ccache from main:
# Actions cache restores only see same-ref + default-branch entries, so these
# are the only entries a brand-new PR's first push can warm-start from (the
# gate jobs carry ccache-nightly-* fallback restore-keys). asan-ubsan doubles
# as the nightly re-proof of the gate sanitizer on main.
CHECKS_NIGHTLY := tsan rtsan static cppcheck coverage-check hardened trivy \
                  gitleaks osv semgrep test-rust test-py debug asan-ubsan
# Checks whose make target compiles C++ through ccache. Drives which nightly
# legs persist a cache -- the scanners and tooling tests would mint empty
# entries, and static (clang-tidy) does not compile through the launcher
# (observed: every ccache-static-* Actions cache was 191 bytes).
CHECKS_CCACHE  := tsan rtsan coverage-check hardened debug asan-ubsan

# ------------------------------------------------------------------------------
# Aggregate targets -- run a whole category or tier
# ------------------------------------------------------------------------------

.PHONY: checks-sanitizers checks-static checks-security checks-hardening
.PHONY: checks-coverage checks-all checks-gate checks-nightly

checks-sanitizers: $(CHECKS_SANITIZER)
checks-static:     $(CHECKS_STATIC)
checks-security:   $(CHECKS_SECURITY)
checks-hardening:  $(CHECKS_HARDENING)
checks-coverage:   $(CHECKS_COVERAGE)
checks-all:        $(CHECKS_ALL)
checks-gate:       $(CHECKS_GATE)
checks-nightly:    $(CHECKS_NIGHTLY)

# ------------------------------------------------------------------------------
# Discovery
# ------------------------------------------------------------------------------

.PHONY: list-checks print-gate-checks print-nightly-checks print-ccache-checks

list-checks:
	@printf 'Checks by category:\n'
	@printf '  sanitizer : %s\n' "$(CHECKS_SANITIZER)"
	@printf '  static    : %s\n' "$(CHECKS_STATIC)"
	@printf '  security  : %s\n' "$(or $(strip $(CHECKS_SECURITY)),(none yet))"
	@printf '  hardening : %s\n' "$(or $(strip $(CHECKS_HARDENING)),(none yet))"
	@printf '  coverage  : %s\n' "$(CHECKS_COVERAGE)"
	@printf 'CI tiers:\n'
	@printf '  gate      : %s\n' "$(CHECKS_GATE)"
	@printf '  nightly   : %s\n' "$(CHECKS_NIGHTLY)"

# JSON arrays for GitHub Actions matrices (single source of truth: the lists
# above). Used by the nightly/gate workflows so adding a check needs no CI edit.
# Built in pure make to avoid shell quoting issues with the embedded quotes.
_empty :=
_space := $(_empty) $(_empty)
_comma := ,
# _json_array: space-separated list -> ["a","b","c"]
_json_array = [$(subst $(_space),$(_comma),$(patsubst %,"%",$(strip $(1))))]

print-gate-checks:
	@printf '%s\n' '$(call _json_array,$(CHECKS_GATE))'
print-nightly-checks:
	@printf '%s\n' '$(call _json_array,$(CHECKS_NIGHTLY))'

print-ccache-checks:
	@printf '%s\n' '$(call _json_array,$(CHECKS_CCACHE))'

endif  # CHECKS_MK_GUARD
