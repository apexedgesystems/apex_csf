# ==============================================================================
# stm32_encryptor_demo - STM32 McuExecutive application for NUCLEO-L476RG
# ==============================================================================
#
# Data channel + command channel + flash key store.
# UART1 (FTDI): SLIP + CRC + AES-256-GCM data channel.
# UART2 (VCP): SLIP + CRC command channel (key mgmt, stats, IV).
# LED heartbeat at 2 Hz, data at 100 Hz, commands at 20 Hz.
#
# Supports bare-metal (default) and FreeRTOS modes via APEX_USE_FREERTOS.
#
# Build (bare-metal):
#   docker compose run --rm -T dev-stm32 make stm32
#
# Build (FreeRTOS):
#   docker compose run --rm -T dev-stm32 make stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
# ==============================================================================

include(apex/All)

option(APEX_USE_FREERTOS "Build with FreeRTOS (vTaskDelayUntil tick source)" OFF)

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
set(APP_DEFS "")

# ------------------------------------------------------------------------------
# FreeRTOS Kernel Sources (optional middleware, from STM32CubeL4)
# ------------------------------------------------------------------------------
#
# FreeRTOS is an opt-in middleware rather than part of the core HAL/CMSIS stack,
# so it stays here instead of in apex_add_firmware_with_stm32().

if (APEX_USE_FREERTOS)
  set(STM32CUBE_PATH "$ENV{STM32CUBE_L4_PATH}")
  if (NOT STM32CUBE_PATH)
    set(STM32CUBE_PATH "/opt/STM32CubeL4")
  endif ()
  set(FREERTOS_DIR "${STM32CUBE_PATH}/Middlewares/Third_Party/FreeRTOS/Source")

  list(
    APPEND
    APP_SOURCES
    "${FREERTOS_DIR}/tasks.c"
    "${FREERTOS_DIR}/list.c"
    "${FREERTOS_DIR}/queue.c"
    "${FREERTOS_DIR}/timers.c"
    "${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c"
    "${FREERTOS_DIR}/portable/MemMang/heap_4.c"
  )
  list(APPEND APP_INC "${FREERTOS_DIR}/include" "${FREERTOS_DIR}/portable/GCC/ARM_CM4F")
  list(APPEND APP_DEFS APEX_USE_FREERTOS)
endif ()

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

apex_add_firmware_with_stm32(
  NAME
  stm32_encryptor_demo
  CUBE
  L4
  MCU
  STM32L476xx
  STARTUP
  startup_stm32l476xx
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
  "${CMAKE_CURRENT_SOURCE_DIR}/STM32L476RG.ld"
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
