# ==============================================================================
# mk/size.mk - Binary size analysis (Google bloaty)
#
# Reads ELF / Mach-O / PE binaries and prints hierarchical size breakdowns
# by symbol, compile unit, archive, or section. Useful for MCU firmware
# (where flash size is the constraining resource) and for tracking the
# binary size of POSIX apps over time.
#
# bloaty is installed in the apex.base image. These targets only invoke
# the tool; they do not build anything.
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

# Default dimensions: compileunits then symbols within each. Good first look.
DIM ?= compileunits,symbols

# Top N rows to print per dimension. Tune for verbosity.
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
# Per-Platform Convenience Wrappers
# ------------------------------------------------------------------------------
# Each looks up the .elf in the platform's firmware/ directory.

size-stm32:
	@test -n "$(FW)" || { printf '$(TERM_RED)[size-stm32]$(TERM_RESET) FW not set. Usage: make size-stm32 FW=<firmware>\n'; exit 1; }
	$(call _size_run,$(STM32_DIR)/firmware/$(FW).elf)

size-arduino:
	@test -n "$(FW)" || { printf '$(TERM_RED)[size-arduino]$(TERM_RESET) FW not set. Usage: make size-arduino FW=<firmware>\n'; exit 1; }
	$(call _size_run,$(ARDUINO_DIR)/firmware/$(FW).elf)

size-pico:
	@test -n "$(FW)" || { printf '$(TERM_RED)[size-pico]$(TERM_RESET) FW not set. Usage: make size-pico FW=<firmware>\n'; exit 1; }
	$(call _size_run,$(PICO_DIR)/firmware/$(FW).elf)

size-esp32:
	@test -n "$(FW)" || { printf '$(TERM_RED)[size-esp32]$(TERM_RESET) FW not set. Usage: make size-esp32 FW=<firmware>\n'; exit 1; }
	$(call _size_run,$(ESP32_DIR)/$(FW).elf)

size-c2000:
	@test -n "$(FW)" || { printf '$(TERM_RED)[size-c2000]$(TERM_RESET) FW not set. Usage: make size-c2000 FW=<firmware>\n'; exit 1; }
	$(call _size_run,$(C2000_DIR)/firmware/$(FW).elf)

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

.PHONY: size size-stm32 size-arduino size-pico size-esp32 size-c2000 size-app size-diff

endif  # SIZE_MK_GUARD
