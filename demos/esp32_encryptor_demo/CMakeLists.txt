# ==============================================================================
# esp32_encryptor_demo - ESP32-S3 McuExecutive application for Arduino Nano ESP32
# ==============================================================================
#
# AES-256-GCM encryption firmware for ESP32-S3 (Arduino Nano ESP32).
# Fourth platform in the Apex CSF encryptor family (STM32, Arduino, Pico, ESP32).
#
# Dual channel: UART0 (FTDI) data, USB CDC (native USB) command.
# SLIP + CRC + AES-256-GCM data pipeline, NVS key storage.
# LED heartbeat at 2 Hz (WS2812 on GPIO48), data at 100 Hz, commands at 20 Hz.
#
# Build:
#   docker compose run --rm -T dev-esp32 make esp32
#
# Flash:
#   docker compose run --rm -T dev-esp32 make esp32-flash \
#     ESP32_FIRMWARE=esp32_encryptor_demo ESP32_PORT=/dev/esp32_0
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
)

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

apex_add_firmware_with_esp32(
  NAME
  esp32_encryptor_demo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/esp32/inc"
  LINK
  executive_mcu
  encryption_mcu
  system_core_protocols_framing_slip
  utilities_checksums_crc
  COMPONENTS
  freertos
  driver
  nvs_flash
  esp_timer
  esptool_py
  IDF_LIBS
  idf::freertos
  idf::driver
  idf::nvs_flash
  idf::esp_timer
)

apex_finalize_firmware()
