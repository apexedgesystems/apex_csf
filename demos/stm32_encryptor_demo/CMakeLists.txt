# ==============================================================================
# stm32_encryptor_demo - STM32 McuExecutive application for NUCLEO boards
# ==============================================================================
#
# Data channel + command channel + flash key store.
# Data UART (FTDI): SLIP + CRC + AES-256-GCM data channel.
# Command UART (ST-Link VCP): SLIP + CRC command channel (key mgmt, stats, IV).
# LED heartbeat at 2 Hz, data at 100 Hz, commands at 20 Hz.
#
# The board is selected by APEX_STM32_BOARD (toolchain cache variable):
#   nucleo_l476rg  STM32L476RG, Cortex-M4F @ 80 MHz   (default)
#   nucleo_f767zi  STM32F767ZI, Cortex-M7 @ 216 MHz
# Execution mode is selected by APEX_USE_FREERTOS (bare-metal default).
#
# Build (bare-metal release package, default board):
#   make release APP=stm32_encryptor_demo
#
# Build (NUCLEO-F767ZI; a build directory holds one board, see the toolchain):
#   make release APP=stm32_encryptor_demo CMAKE_EXTRA_ARGS="-DAPEX_STM32_BOARD=nucleo_f767zi"
#
# Build (FreeRTOS, development):
#   make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
# ==============================================================================

include(apex/All)

option(APEX_USE_FREERTOS "Build with FreeRTOS (vTaskDelayUntil tick source)" OFF)

# ------------------------------------------------------------------------------
# Board Table
# ------------------------------------------------------------------------------
#
# One row per NUCLEO: Cube edition, MCU define, startup file, linker script,
# FreeRTOS port directory, and the board definition inc/boards/Board.hpp keys
# on. APEX_STM32_BOARD itself is declared by cmake/toolchains/stm32-gcc.cmake,
# which derives the CPU flags from the same value.

if (APEX_STM32_BOARD STREQUAL "nucleo_l476rg")
  set(BOARD_CUBE L4)
  set(BOARD_MCU STM32L476xx)
  set(BOARD_STARTUP startup_stm32l476xx)
  set(BOARD_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/STM32L476RG.ld")
  set(BOARD_FREERTOS_PORT "portable/GCC/ARM_CM4F")
  set(BOARD_DEFINE APEX_BOARD_NUCLEO_L476RG)
elseif (APEX_STM32_BOARD STREQUAL "nucleo_f767zi")
  set(BOARD_CUBE F7)
  set(BOARD_MCU STM32F767xx)
  set(BOARD_STARTUP startup_stm32f767xx)
  set(BOARD_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/STM32F767ZI.ld")
  set(BOARD_FREERTOS_PORT "portable/GCC/ARM_CM7/r0p1")
  set(BOARD_DEFINE APEX_BOARD_NUCLEO_F767ZI)
else ()
  message(
    FATAL_ERROR "stm32_encryptor_demo: no board row for APEX_STM32_BOARD='${APEX_STM32_BOARD}'"
  )
endif ()

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

set(APP_INC "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
            "${CMAKE_SOURCE_DIR}/src/system/core/hal/stm32/inc"
)
set(APP_DEFS ${BOARD_DEFINE})

# ------------------------------------------------------------------------------
# FreeRTOS Kernel Sources (optional middleware, from STM32CubeL4)
# ------------------------------------------------------------------------------
#
# FreeRTOS is an opt-in middleware rather than part of the core HAL/CMSIS stack,
# so it stays here instead of in apex_add_firmware_with_stm32().

if (APEX_USE_FREERTOS)
  set(STM32CUBE_PATH "$ENV{STM32CUBE_${BOARD_CUBE}_PATH}")
  if (NOT STM32CUBE_PATH)
    set(STM32CUBE_PATH "/opt/STM32Cube${BOARD_CUBE}")
  endif ()
  set(FREERTOS_DIR "${STM32CUBE_PATH}/Middlewares/Third_Party/FreeRTOS/Source")

  list(
    APPEND
    APP_SOURCES
    "${FREERTOS_DIR}/tasks.c"
    "${FREERTOS_DIR}/list.c"
    "${FREERTOS_DIR}/queue.c"
    "${FREERTOS_DIR}/timers.c"
    "${FREERTOS_DIR}/${BOARD_FREERTOS_PORT}/port.c"
    "${FREERTOS_DIR}/portable/MemMang/heap_4.c"
  )
  list(APPEND APP_INC "${FREERTOS_DIR}/include" "${FREERTOS_DIR}/${BOARD_FREERTOS_PORT}")
  list(APPEND APP_DEFS APEX_USE_FREERTOS)
endif ()

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

apex_add_firmware_with_stm32(
  NAME
  stm32_encryptor_demo
  CUBE
  ${BOARD_CUBE}
  MCU
  ${BOARD_MCU}
  STARTUP
  ${BOARD_STARTUP}
  HAL_MODULES
  cortex
  flash
  flash_ex
  gpio
  rcc
  rcc_ex
  pwr
  pwr_ex
  dma
  dma_ex
  uart
  uart_ex
  LINKER_SCRIPT
  "${BOARD_LINKER_SCRIPT}"
  SRC
  ${APP_SOURCES}
  INC
  ${APP_INC}
  LINK
  executive_mcu
  encryption_mcu
  system_core_protocols_framing_slip
  utilities_checksums_crc
  DEFS
  ${APP_DEFS}
)

apex_finalize_firmware()
