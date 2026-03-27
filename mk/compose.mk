# ==============================================================================
# mk/compose.mk - Docker Compose build wrappers
#
# Runs make targets inside the correct Docker Compose service so developers
# don't have to remember which service maps to which build.
#
# Usage:
#   make compose-debug                          Native debug via dev-cuda
#   make compose-stm32                          STM32 firmware via dev-stm32
#   make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
#   make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor
#   make compose-jetson-release                 Jetson cross-compile via dev-jetson
# ==============================================================================

ifndef COMPOSE_MK_GUARD
COMPOSE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# _compose_run: Run a make target inside a docker compose service
# Usage: $(call _compose_run,display_name,service,target[,extra_make_args])
define _compose_run
	$(call log,compose,$(1) [$(2)])
	@docker compose run --rm -T $(2) make $(3) \
	  VERBOSE=$(VERBOSE) CMAKE_EXTRA_ARGS="$(CMAKE_EXTRA_ARGS)" $(4)
endef

# _compose_target: Generate a compose-<suffix> target
# $(1) = target suffix, $(2) = display name, $(3) = service, $(4) = make target
define _compose_target
.PHONY: compose-$(1)
compose-$(1):
	$$(call _compose_run,$(2),$(3),$(4))
endef

# ------------------------------------------------------------------------------
# Native Builds (dev-cuda)
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,debug,native debug,dev-cuda,debug))
$(eval $(call _compose_target,release,native release,dev-cuda,release))
$(eval $(call _compose_target,docs,documentation,dev-cuda,docs))

# ------------------------------------------------------------------------------
# Testing and Quality (dev-cuda)
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,test,tests (serial),dev-cuda,test))
$(eval $(call _compose_target,testp,tests (parallel),dev-cuda,testp))
$(eval $(call _compose_target,coverage,coverage,dev-cuda,coverage))
$(eval $(call _compose_target,format,format (auto-fix),dev-cuda,format))
$(eval $(call _compose_target,format-check,format (check only),dev-cuda,format-check))
$(eval $(call _compose_target,static,static analysis,dev-cuda,static))
$(eval $(call _compose_target,asan,AddressSanitizer,dev-cuda,asan))
$(eval $(call _compose_target,tsan,ThreadSanitizer,dev-cuda,tsan))
$(eval $(call _compose_target,ubsan,UBSanitizer,dev-cuda,ubsan))

# ------------------------------------------------------------------------------
# Tools (dev-cuda)
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,tools,all tools,dev-cuda,tools))
$(eval $(call _compose_target,tools-cpp,C++ tools,dev-cuda,tools-cpp))
$(eval $(call _compose_target,tools-py,Python tools,dev-cuda,tools-py))
$(eval $(call _compose_target,tools-rust,Rust tools,dev-cuda,tools-rust))

# ------------------------------------------------------------------------------
# Cross-Compilation
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,jetson-debug,Jetson debug,dev-jetson,jetson-debug))
$(eval $(call _compose_target,jetson-release,Jetson release,dev-jetson,jetson-release))
$(eval $(call _compose_target,rpi-debug,Raspberry Pi debug,dev-rpi,rpi-debug))
$(eval $(call _compose_target,rpi-release,Raspberry Pi release,dev-rpi,rpi-release))
$(eval $(call _compose_target,riscv-debug,RISC-V 64 debug,dev-riscv64,riscv-debug))
$(eval $(call _compose_target,riscv-release,RISC-V 64 release,dev-riscv64,riscv-release))

# ------------------------------------------------------------------------------
# Firmware Build Targets
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,stm32,STM32 firmware,dev-stm32,stm32))
$(eval $(call _compose_target,arduino,Arduino firmware,dev-arduino,arduino))
$(eval $(call _compose_target,pico,Pico firmware,dev-pico,pico))
$(eval $(call _compose_target,esp32,ESP32 firmware,dev-esp32,esp32))
$(eval $(call _compose_target,c2000,C2000 firmware,dev-c2000,c2000))

# ------------------------------------------------------------------------------
# Firmware Flash and Reset
# ------------------------------------------------------------------------------
# Kept explicit because each platform passes different device-selector variables.

.PHONY: compose-stm32-flash compose-stm32-reset
compose-stm32-flash:
	$(call _compose_run,STM32 flash,dev-stm32,stm32-flash,STM32_FIRMWARE="$(STM32_FIRMWARE)" STM32_SERIAL="$(STM32_SERIAL)")
compose-stm32-reset:
	$(call _compose_run,STM32 reset,dev-stm32,stm32-reset,STM32_SERIAL="$(STM32_SERIAL)")

.PHONY: compose-arduino-flash compose-arduino-reset
compose-arduino-flash:
	$(call _compose_run,Arduino flash,dev-arduino,arduino-flash,ARDUINO_FIRMWARE="$(ARDUINO_FIRMWARE)" ARDUINO_PORT="$(ARDUINO_PORT)")
compose-arduino-reset:
	$(call _compose_run,Arduino reset,dev-arduino,arduino-reset,ARDUINO_PORT="$(ARDUINO_PORT)")

.PHONY: compose-pico-flash compose-pico-reset
compose-pico-flash:
	$(call _compose_run,Pico flash,dev-pico,pico-flash,PICO_FIRMWARE="$(PICO_FIRMWARE)" PICO_ADDRESS="$(PICO_ADDRESS)")
compose-pico-reset:
	$(call _compose_run,Pico reset,dev-pico,pico-reset,PICO_ADDRESS="$(PICO_ADDRESS)")

.PHONY: compose-esp32-flash compose-esp32-reset
compose-esp32-flash:
	$(call _compose_run,ESP32 flash,dev-esp32,esp32-flash,ESP32_FIRMWARE="$(ESP32_FIRMWARE)" ESP32_PORT="$(ESP32_PORT)")
compose-esp32-reset:
	$(call _compose_run,ESP32 reset,dev-esp32,esp32-reset,ESP32_PORT="$(ESP32_PORT)")

.PHONY: compose-c2000-flash compose-c2000-reset
compose-c2000-flash:
	$(call _compose_run,C2000 flash,dev-c2000,c2000-flash,C2000_FIRMWARE="$(C2000_FIRMWARE)" C2000_CCXML="$(C2000_CCXML)" C2000_SERIAL="$(C2000_SERIAL)")
compose-c2000-reset:
	$(call _compose_run,C2000 reset,dev-c2000,c2000-reset,C2000_CCXML="$(C2000_CCXML)" C2000_SERIAL="$(C2000_SERIAL)")

endif  # COMPOSE_MK_GUARD
