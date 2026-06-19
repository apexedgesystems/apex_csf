# ==============================================================================
# pico_encryptor_demo - RP2040 McuExecutive application for Raspberry Pi Pico
# ==============================================================================
#
# AES-256-GCM encryption firmware for Raspberry Pi Pico (RP2040).
# Third platform in the Apex CSF encryptor family (STM32, Arduino, Pico).
#
# Dual UART: UART0 (FTDI) data channel, UART1 (USB-UART) command channel.
# SLIP + CRC + AES-256-GCM data pipeline, flash key storage.
# LED heartbeat at 2 Hz, data at 100 Hz, commands at 20 Hz.
#
# Build:
#   docker compose run --rm -T dev-pico make pico
#
# Flash:
#   docker compose run --rm -T dev-pico make pico-flash PICO_FIRMWARE=pico_encryptor_demo
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Application Sources
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

apex_add_firmware_with_pico(
  NAME
  pico_encryptor_demo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/pico/inc"
  LINK
  executive_mcu
  encryption_mcu
  system_core_protocols_framing_slip
  utilities_checksums_crc
  PICO_LIBS
  pico_stdlib
  hardware_uart
  hardware_flash
  hardware_gpio
  hardware_timer
  hardware_clocks
  hardware_irq
  hardware_sync
)

apex_finalize_firmware()
