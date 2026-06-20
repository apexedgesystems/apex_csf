# ==============================================================================
# c2000_encryptor_demo - AES-256-GCM encryptor + CAN demo for LAUNCHXL-F280049C
# ==============================================================================
#
# Single-UART encryptor with CAN internal loopback demo.
# Uses C2000Ware driverlib API (not bitfield headers).
#
# Build:
#   make release APP=c2000_encryptor_demo
#
# Flash:
#   make compose-c2000-flash C2000_FIRMWARE=c2000_encryptor_demo \
#     C2000_CCXML=apps/c2000_encryptor_demo/LAUNCHXL_F280049C.ccxml
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Application Sources
# ------------------------------------------------------------------------------
#
# device.c is local (copied from C2000Ware, patched for LAUNCHXL-F280049C).

set(APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/cxx_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/device.c"
)

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

apex_add_firmware_with_c2000(
  NAME
  c2000_encryptor_demo
  DEVICE
  f28004x
  MCU
  _LAUNCHXL_F280049C
  LINKER_SCRIPT
  "${CMAKE_CURRENT_SOURCE_DIR}/F280049C.cmd"
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  "${CMAKE_SOURCE_DIR}/src/system/core/hal/c2000/inc"
  "${CMAKE_SOURCE_DIR}"
  DEFS
  CPU1
  _LAUNCHXL_F280049C
  _FLASH
  USE_INTOSC
)

apex_finalize_firmware()
