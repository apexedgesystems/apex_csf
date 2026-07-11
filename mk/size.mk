# ==============================================================================
# mk/size.mk - Binary size analysis (Google bloaty)
#
# Wraps bloaty for ELF / Mach-O / PE binaries with per-platform shortcuts.
# bloaty is installed in apex.base.
#
# Usage:
#   make size FILE=path/to/binary [DIM=compileunits,symbols] [N=20]
#   make size-stm32 FW=stm32_encryptor_demo
#   make size-arduino FW=arduino_encryptor_demo
#   make size-pico FW=pico_encryptor_demo
#   make size-esp32 FW=esp32_encryptor_demo
#   make size-c2000 FW=c2000_encryptor_demo
#   make size-app APP=ApexEdgeDemo
#   make size-diff NEW=path/to/new OLD=path/to/old
# ==============================================================================

ifndef SIZE_MK_GUARD
SIZE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

# Default dimensions: compile units then symbols.
DIM ?= compileunits,symbols

# Top N rows to print per dimension.
N ?= 25

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# _size_run: run bloaty on a single file with the configured DIM and N
# Usage: $(call _size_run,path)
define _size_run
	@test -f "$(1)" || { printf '$(TERM_RED)[size]$(TERM_RESET) not found: %s\n' "$(1)"; exit 1; }
	$(call log,size,$(1))
	@bloaty -d "$(DIM)" -n $(N) "$(1)"
endef

# ------------------------------------------------------------------------------
# Generic Target
# ------------------------------------------------------------------------------

# Generic: bloaty against any binary path
size:
	@test -n "$(FILE)" || { printf '$(TERM_RED)[size]$(TERM_RESET) FILE not set. Usage: make size FILE=path/to/binary [DIM=compileunits,symbols] [N=20]\n'; exit 1; }
	$(call _size_run,$(FILE))

# ------------------------------------------------------------------------------
# Per-Platform Convenience Wrappers (registry-driven)
# ------------------------------------------------------------------------------
# One wrapper per fw-ship registry row -- a new firmware platform gets its
# size-<plat> for free. The .elf lives under the platform build dir in
# $(P_<p>_FWELF) (default firmware/; `.` = build root, e.g. ESP-IDF).

_FW_PLATFORMS := $(foreach p,$(PLATFORMS),$(if $(filter fw-ship,$(P_$(p)_ROLE)),$(p)))
_fwelf_dir = $(if $(filter .,$(P_$(1)_FWELF)),,firmware/)

define _SIZE_template
size-$(1):
	@test -n "$$(FW)" || { printf '$(TERM_RED)[size-$(1)]$(TERM_RESET) FW not set. Usage: make size-$(1) FW=<firmware>\n'; exit 1; }
	$$(call _size_run,build/$(P_$(1)_PRESET)/$(call _fwelf_dir,$(1))$$(FW).elf)
endef

$(foreach p,$(_FW_PLATFORMS),$(eval $(call _SIZE_template,$(p))))

# Native POSIX app under the host release build dir
size-app:
	@test -n "$(APP)" || { printf '$(TERM_RED)[size-app]$(TERM_RESET) APP not set. Usage: make size-app APP=<binary>\n'; exit 1; }
	$(call _size_run,$(HOST_RELEASE_DIR)/bin/$(APP))

# ------------------------------------------------------------------------------
# Diff
# ------------------------------------------------------------------------------

size-diff:
	@test -n "$(NEW)" || { printf '$(TERM_RED)[size-diff]$(TERM_RESET) NEW not set\n'; exit 1; }
	@test -n "$(OLD)" || { printf '$(TERM_RED)[size-diff]$(TERM_RESET) OLD not set\n'; exit 1; }
	@test -f "$(NEW)" || { printf '$(TERM_RED)[size-diff]$(TERM_RESET) not found: %s\n' "$(NEW)"; exit 1; }
	@test -f "$(OLD)" || { printf '$(TERM_RED)[size-diff]$(TERM_RESET) not found: %s\n' "$(OLD)"; exit 1; }
	$(call log,size-diff,$(NEW) vs $(OLD))
	@bloaty -d "$(DIM)" -n $(N) "$(NEW)" -- "$(OLD)"

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: size $(addprefix size-,$(_FW_PLATFORMS)) size-app size-diff

endif  # SIZE_MK_GUARD
