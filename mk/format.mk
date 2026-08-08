# ==============================================================================
# mk/format.mk - Code formatting and linting
#
# Git-agnostic formatting using pre-commit. Scans filesystem (tracked and
# untracked files), prunes build/cache directories, and batches paths to
# avoid argument limits.
# ==============================================================================

ifndef FORMAT_MK_GUARD
FORMAT_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

# Directories to scan (override: make format PC_SCOPE="src prod")
PC_SCOPE ?= .

# Find command with pruning for build/cache directories. Tool-managed
# directories (venvs, caches, node_modules) prune by NAME so nested
# instances (e.g. tools/py/.venv) are excluded too -- a local venv's
# site-packages swamps the sweep with thousands of third-party files.
PRECOMMIT_FIND := find $(PC_SCOPE) \
  \( -path ./build -o -path './cmake-build*' -o -path ./dist -o -path ./out \
     -o -name node_modules -o -name .git -o -name .hg -o -name .venv \
     -o -name .mypy_cache -o -name .pytest_cache -o -name .ruff_cache \
     -o -name .cache -o -name __pycache__ \) -prune -o \
  -type f -not -name 'compile_commands.json' -print0

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# Run pre-commit on all files at once
# Usage: $(call _run_precommit,extra-args)
define _run_precommit
	@$(PRECOMMIT_FIND) | xargs -0 -r pre-commit run $(1) --files
endef

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

# Auto-fix formatting issues
format:
	$(call log,format,Running formatters with auto-fix)
	$(call _run_precommit,)

# Check-only mode (no fixes), show diffs on failure
format-check:
	$(call log,format,Checking formatting (no fixes))
	$(call _run_precommit,--show-diff-on-failure)

# Run include-what-you-use analysis (requires build dir, invoked manually)
lint-iwyu:
	$(call log,format,Running include-what-you-use)
	@pre-commit run --hook-stage manual iwyu-local

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: format format-check lint-iwyu

endif  # FORMAT_MK_GUARD