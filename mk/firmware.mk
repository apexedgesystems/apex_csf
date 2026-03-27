# ==============================================================================
# mk/firmware.mk - Firmware flash and reset utilities
#
# Targets for flashing and resetting bare-metal firmware via programmer.
# All targets follow the same pattern:
#   <platform>-flash: validate → log → flash tool → log_ok
#   <platform>-reset: log → reset tool → log_ok
#
# Each platform requires <PLATFORM>_FIRMWARE and accepts an optional device
# selector for multi-device setups:
#   STM32:   STM32_SERIAL   (ST-Link serial number, e.g. "483F6B066757")
#   Arduino: ARDUINO_PORT   (serial port, e.g. "/dev/ttyACM1")
#   Pico:    PICO_ADDRESS   (USB bus:addr, e.g. "1:4")
#   ESP32:   ESP32_PORT     (serial port, e.g. "/dev/esp32_0")
#
# Run through Docker compose:
#   make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor
#   make compose-arduino-flash ARDUINO_FIRMWARE=arduino_encryptor ARDUINO_PORT=/dev/ttyACM1
#   make compose-pico-flash PICO_FIRMWARE=pico_encryptor
#   make compose-esp32-flash ESP32_FIRMWARE=esp32_encryptor ESP32_PORT=/dev/esp32_0
# ==============================================================================

ifndef FIRMWARE_MK_GUARD
FIRMWARE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# STM32 Configuration
# ------------------------------------------------------------------------------

STM32_FIRMWARE ?=
STM32_FLASH_BASE ?= 0x08000000
STM32_SERIAL ?=

# ------------------------------------------------------------------------------
# STM32 Targets (requires st-flash)
# ------------------------------------------------------------------------------

stm32-flash:
	@test -n "$(STM32_FIRMWARE)" || { printf '[stm32-flash] STM32_FIRMWARE not set\n'; exit 1; }
	$(call log,stm32-flash,Flashing $(STM32_FIRMWARE))
	@st-flash $(if $(STM32_SERIAL),--serial $(STM32_SERIAL)) \
	  write $(STM32_DIR)/firmware/$(STM32_FIRMWARE).bin $(STM32_FLASH_BASE)
	$(call log_ok,stm32-flash,$(STM32_FIRMWARE) flashed)

stm32-reset:
	$(call log,stm32-reset,Resetting STM32)
	@st-flash $(if $(STM32_SERIAL),--serial $(STM32_SERIAL)) reset
	$(call log_ok,stm32-reset,Reset complete)

# ------------------------------------------------------------------------------
# Arduino Configuration
# ------------------------------------------------------------------------------

ARDUINO_FIRMWARE ?=
ARDUINO_PORT ?=

# ------------------------------------------------------------------------------
# Arduino Targets (requires avrdude)
# ------------------------------------------------------------------------------

arduino-flash:
	@test -n "$(ARDUINO_FIRMWARE)" || { printf '[arduino-flash] ARDUINO_FIRMWARE not set\n'; exit 1; }
	@test -n "$(ARDUINO_PORT)" || { printf '[arduino-flash] ARDUINO_PORT not set\n'; exit 1; }
	$(call log,arduino-flash,Flashing $(ARDUINO_FIRMWARE))
	@avrdude -c arduino -p m328p -P $(ARDUINO_PORT) -b 115200 \
	  -U flash:w:$(ARDUINO_DIR)/firmware/$(ARDUINO_FIRMWARE).hex:i
	$(call log_ok,arduino-flash,$(ARDUINO_FIRMWARE) flashed)

arduino-reset:
	@test -n "$(ARDUINO_PORT)" || { printf '[arduino-reset] ARDUINO_PORT not set\n'; exit 1; }
	$(call log,arduino-reset,Resetting Arduino)
	@python3 -c "import serial; s=serial.Serial('$(ARDUINO_PORT)',1200); s.close()"
	@sleep 2
	$(call log_ok,arduino-reset,Reset complete)

# ------------------------------------------------------------------------------
# Pico Configuration
# ------------------------------------------------------------------------------

PICO_FIRMWARE ?=
PICO_ADDRESS ?=

# ------------------------------------------------------------------------------
# Pico Targets (requires picotool)
# ------------------------------------------------------------------------------

pico-flash:
	@test -n "$(PICO_FIRMWARE)" || { printf '[pico-flash] PICO_FIRMWARE not set\n'; exit 1; }
	$(call log,pico-flash,Flashing $(PICO_FIRMWARE))
	@picotool reboot -f -u $(if $(PICO_ADDRESS),--bus-addr $(PICO_ADDRESS)) 2>/dev/null; sleep 5
	@picotool load $(PICO_DIR)/firmware/$(PICO_FIRMWARE).elf -f \
	  $(if $(PICO_ADDRESS),--bus-addr $(PICO_ADDRESS))
	$(call log_ok,pico-flash,$(PICO_FIRMWARE) flashed)

pico-reset:
	$(call log,pico-reset,Resetting Pico)
	@picotool reboot $(if $(PICO_ADDRESS),--bus-addr $(PICO_ADDRESS))
	$(call log_ok,pico-reset,Reset complete)

# ------------------------------------------------------------------------------
# ESP32 Configuration
# ------------------------------------------------------------------------------

ESP32_FIRMWARE ?=
ESP32_PORT ?=

# ------------------------------------------------------------------------------
# ESP32 Targets (requires esptool.py)
# ------------------------------------------------------------------------------

esp32-flash:
	@test -n "$(ESP32_FIRMWARE)" || { printf '[esp32-flash] ESP32_FIRMWARE not set\n'; exit 1; }
	@test -n "$(ESP32_PORT)" || { printf '[esp32-flash] ESP32_PORT not set\n'; exit 1; }
	$(call log,esp32-flash,Flashing $(ESP32_FIRMWARE))
	@esptool.py --chip esp32s3 --port $(ESP32_PORT) --baud 921600 \
	  write_flash -z \
	  0x0 $(ESP32_DIR)/bootloader/bootloader.bin \
	  0x8000 $(ESP32_DIR)/partition_table/partition-table.bin \
	  0x10000 $(ESP32_DIR)/$(ESP32_FIRMWARE).bin
	$(call log_ok,esp32-flash,$(ESP32_FIRMWARE) flashed)

esp32-reset:
	@test -n "$(ESP32_PORT)" || { printf '[esp32-reset] ESP32_PORT not set\n'; exit 1; }
	$(call log,esp32-reset,Resetting ESP32)
	@esptool.py --chip esp32s3 --port $(ESP32_PORT) run
	$(call log_ok,esp32-reset,Reset complete)

# ------------------------------------------------------------------------------
# C2000 Configuration
# ------------------------------------------------------------------------------

C2000_FIRMWARE ?=
C2000_SERIAL ?=

# CCXML configuration for XDS110 on LAUNCHXL-F280049C (from C2000Ware SDK)
C2000_CCXML ?=

# ------------------------------------------------------------------------------
# C2000 Targets (requires UniFlash dslite.sh)
# ------------------------------------------------------------------------------

c2000-flash:
	@test -n "$(C2000_FIRMWARE)" || { printf '[c2000-flash] C2000_FIRMWARE not set\n'; exit 1; }
	@test -n "$(C2000_CCXML)" || { printf '[c2000-flash] C2000_CCXML not set\n'; exit 1; }
	$(call log,c2000-flash,Flashing $(C2000_FIRMWARE))
	@dslite.sh --config=$(C2000_CCXML) \
	  -f $(C2000_DIR)/firmware/$(C2000_FIRMWARE).elf \
	  -e -u
	$(call log_ok,c2000-flash,$(C2000_FIRMWARE) flashed)

c2000-reset:
	$(call log,c2000-reset,Resetting C2000)
	@dslite.sh --config=$(C2000_CCXML) \
	  -r system_reset
	$(call log_ok,c2000-reset,Reset complete)

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: stm32-flash stm32-reset
.PHONY: arduino-flash arduino-reset
.PHONY: pico-flash pico-reset
.PHONY: esp32-flash esp32-reset
.PHONY: c2000-flash c2000-reset

endif  # FIRMWARE_MK_GUARD
