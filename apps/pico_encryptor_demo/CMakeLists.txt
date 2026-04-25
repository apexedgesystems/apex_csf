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
# Pico SDK Initialization
# ------------------------------------------------------------------------------

set(PICO_SDK_PATH
    "$ENV{PICO_SDK_PATH}"
    CACHE PATH "Pico SDK path"
)
if (NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH "/opt/pico-sdk")
endif ()

if (EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
  include("${PICO_SDK_PATH}/pico_sdk_init.cmake")
  pico_sdk_init()
  set(PICO_SDK_AVAILABLE TRUE)
  message(STATUS "[pico_encryptor_demo] Pico SDK found at ${PICO_SDK_PATH}")
else ()
  set(PICO_SDK_AVAILABLE FALSE)
  message(WARNING "[pico_encryptor_demo] Pico SDK not found at ${PICO_SDK_PATH}")
endif ()

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
  pico_encryptor_demo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/pico/inc"
  LINK
  executive_mcu
  encryption_lite
  system_core_protocols_framing_slip
  utilities_checksums_crc
)

# ------------------------------------------------------------------------------
# Pico SDK Integration (when SDK is available in Docker container)
# ------------------------------------------------------------------------------

if (PICO_SDK_AVAILABLE)
  target_link_libraries(
    pico_encryptor_demo
    PRIVATE pico_stdlib
            hardware_uart
            hardware_flash
            hardware_gpio
            hardware_timer
            hardware_clocks
            hardware_irq
            hardware_sync
  )

  # USB CDC enabled for picotool remote BOOTSEL (standard flash workflow)
  # UART stdio disabled (we manage UART directly)
  pico_enable_stdio_usb(pico_encryptor_demo 1)
  pico_enable_stdio_uart(pico_encryptor_demo 0)

  # Generate .uf2, .hex, .bin, .map, .dis
  pico_add_extra_outputs(pico_encryptor_demo)
endif ()

# ------------------------------------------------------------------------------
# Finalize
# ------------------------------------------------------------------------------

apex_finalize_firmware()
