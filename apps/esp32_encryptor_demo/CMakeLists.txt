# ==============================================================================
# esp32_encryptor_demo - ESP32-S3 LiteExecutive application for Arduino Nano ESP32
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
# ESP-IDF Initialization (similar to Pico SDK's pico_sdk_init())
# ------------------------------------------------------------------------------

if (DEFINED ENV{IDF_PATH})
  include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

  idf_build_process(
    "esp32s3"
    COMPONENTS
    freertos
    driver
    nvs_flash
    esp_timer
    esptool_py
    SDKCONFIG
    "${CMAKE_BINARY_DIR}/sdkconfig"
    SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    BUILD_DIR
    "${CMAKE_BINARY_DIR}"
  )

  set(ESP_IDF_AVAILABLE TRUE)
  message(STATUS "[esp32_encryptor_demo] ESP-IDF found at $ENV{IDF_PATH}")
else ()
  set(ESP_IDF_AVAILABLE FALSE)
  message(WARNING "[esp32_encryptor_demo] ESP-IDF not found (IDF_PATH not set)")
endif ()

# ------------------------------------------------------------------------------
# Application Sources (4 files)
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

apex_add_firmware(
  NAME
  esp32_encryptor_demo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/esp32/inc"
  LINK
  executive_lite
  encryption_lite
  system_core_protocols_framing_slip
  utilities_checksums_crc
)

# ------------------------------------------------------------------------------
# ESP-IDF Integration (when IDF is available in Docker container)
# ------------------------------------------------------------------------------

if (ESP_IDF_AVAILABLE)
  target_link_libraries(
    esp32_encryptor_demo PRIVATE idf::freertos idf::driver idf::nvs_flash idf::esp_timer
  )

  idf_build_executable(esp32_encryptor_demo)
endif ()

# ------------------------------------------------------------------------------
# Finalize
# ------------------------------------------------------------------------------

apex_finalize_firmware()
