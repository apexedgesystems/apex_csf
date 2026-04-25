# ==============================================================================
# arduino_encryptor_demo - AVR McuExecutive application for Arduino Uno R3
# ==============================================================================
#
# Port of stm32_encryptor to ATmega328P (8-bit AVR, 32 KB flash, 2 KB SRAM).
# Single UART (USART0) multiplexed via channel prefix byte inside SLIP frames:
#   0x00 = data channel (encrypt pipeline)
#   0x01 = command channel (key management, diagnostics)
#
# No vendor HAL SDK needed -- register definitions come from avr-libc.
#
# Build:
#   docker compose run --rm -T dev-arduino make arduino
#
# Flash:
#   docker compose run --rm -T dev-arduino make arduino-flash ARDUINO_FIRMWARE=arduino_encryptor_demo
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Application Sources (5 files)
# ------------------------------------------------------------------------------

set(APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EncryptorEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/KeyStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CommandDeck.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cxx_stubs.cpp"
)

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

apex_add_firmware(
  NAME
  arduino_encryptor_demo
  SRC
  ${APP_SOURCES}
  MCU
  atmega328p
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/avr/inc"
  LINK
  executive_mcu
  encryption_lite
  system_core_protocols_framing_slip
  utilities_checksums_crc
)

# ------------------------------------------------------------------------------
# Finalize
# ------------------------------------------------------------------------------

apex_finalize_firmware()
