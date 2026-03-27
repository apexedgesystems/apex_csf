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
# C2000Ware SDK Path
# ------------------------------------------------------------------------------

set(C2000WARE_PATH "$ENV{C2000WARE_ROOT}")
if (NOT C2000WARE_PATH)
  set(C2000WARE_PATH "/opt/ti/c2000ware-core-sdk")
endif ()

# Device support paths
set(C2000_DEVICE_SUPPORT "${C2000WARE_PATH}/device_support/f28004x")
set(C2000_COMMON "${C2000_DEVICE_SUPPORT}/common")
set(C2000_DRIVERLIB "${C2000WARE_PATH}/driverlib/f28004x/driverlib")

# ------------------------------------------------------------------------------
# C2000Ware Sources (driverlib API only, no bitfield headers)
# ------------------------------------------------------------------------------

# device.c is local (copied from C2000Ware, patched for LAUNCHXL-F280049C)
set(C2000WARE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/device.c"
                      "${C2000_COMMON}/source/f28004x_codestartbranch.asm"
)

# C2000Ware include paths
set(C2000WARE_INC "${C2000_COMMON}/include" "${C2000_DRIVERLIB}" "${C2000_DRIVERLIB}/inc")

# ------------------------------------------------------------------------------
# Application Sources
# ------------------------------------------------------------------------------

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cxx_stubs.cpp"
)

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

set(FIRMWARE_SRC ${APP_SOURCES} ${C2000WARE_SOURCES})
set(FIRMWARE_INC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${CMAKE_SOURCE_DIR}/src/system/core/hal/c2000/inc"
    "${CMAKE_SOURCE_DIR}" ${C2000WARE_INC}
)

apex_add_firmware(
  NAME
  c2000_encryptor_demo
  SRC
  ${FIRMWARE_SRC}
  LINKER_SCRIPT
  "${CMAKE_CURRENT_SOURCE_DIR}/F280049C.cmd"
  MCU
  _LAUNCHXL_F280049C
  INC
  ${FIRMWARE_INC}
  DEFS
  CPU1
  _LAUNCHXL_F280049C
  _FLASH
  USE_INTOSC
)

# Entry point must match codestart label in f28004x_codestartbranch.asm
target_link_options(
  c2000_encryptor_demo PRIVATE "-ecode_start"
)

# Link pre-compiled driverlib (GPIO, SysCtl, SCI, CAN, Timer, Flash)
set(C2000_DRIVERLIB_LIB
    "${C2000WARE_PATH}/driverlib/f28004x/driverlib/ccs/Release/driverlib_coff.lib"
)
target_link_options(
  c2000_encryptor_demo PRIVATE "-l${C2000_DRIVERLIB_LIB}"
)

# ------------------------------------------------------------------------------
# Finalize
# ------------------------------------------------------------------------------

apex_finalize_firmware()
