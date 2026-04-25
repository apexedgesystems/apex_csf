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
# STM32CubeL4 SDK Path
# ------------------------------------------------------------------------------

set(STM32CUBE_PATH "$ENV{STM32CUBE_L4_PATH}")
if (NOT STM32CUBE_PATH)
  set(STM32CUBE_PATH "/opt/STM32CubeL4")
endif ()

# ------------------------------------------------------------------------------
# STM32 HAL Sources (from STM32CubeL4)
# ------------------------------------------------------------------------------

set(STM32_HAL_DIR "${STM32CUBE_PATH}/Drivers/STM32L4xx_HAL_Driver")

set(STM32_HAL_SOURCES
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_cortex.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_flash.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_flash_ex.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_gpio.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_rcc.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_rcc_ex.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_pwr.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_pwr_ex.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_dma.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_dma_ex.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_uart.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_uart_ex.c"
)

# CMSIS files (system + official GCC startup)
set(CMSIS_DIR "${STM32CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32L4xx")
set(STM32_SYSTEM_SOURCES "${CMSIS_DIR}/Source/Templates/system_stm32l4xx.c"
                         "${CMSIS_DIR}/Source/Templates/gcc/startup_stm32l476xx.s"
)

# STM32Cube include paths
set(STM32_INC "${STM32CUBE_PATH}/Drivers/CMSIS/Include" "${CMSIS_DIR}/Include"
              "${STM32_HAL_DIR}/Inc"
)

# ------------------------------------------------------------------------------
# FreeRTOS Kernel Sources (from STM32CubeL4, when enabled)
# ------------------------------------------------------------------------------

if (APEX_USE_FREERTOS)
  set(FREERTOS_DIR "${STM32CUBE_PATH}/Middlewares/Third_Party/FreeRTOS/Source")

  set(FREERTOS_SOURCES
      "${FREERTOS_DIR}/tasks.c" "${FREERTOS_DIR}/list.c" "${FREERTOS_DIR}/queue.c"
      "${FREERTOS_DIR}/timers.c" "${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c"
      "${FREERTOS_DIR}/portable/MemMang/heap_4.c"
  )

  set(FREERTOS_INC "${FREERTOS_DIR}/include" "${FREERTOS_DIR}/portable/GCC/ARM_CM4F")
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

set(FIRMWARE_SRC ${APP_SOURCES} ${STM32_HAL_SOURCES} ${STM32_SYSTEM_SOURCES})
set(FIRMWARE_INC "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
                 "${CMAKE_SOURCE_DIR}/src/system/core/hal/stm32/inc"
)
set(FIRMWARE_DEFS "")

if (APEX_USE_FREERTOS)
  list(APPEND FIRMWARE_SRC ${FREERTOS_SOURCES})
  list(APPEND FIRMWARE_INC ${FREERTOS_INC})
  list(APPEND FIRMWARE_DEFS APEX_USE_FREERTOS)
endif ()

apex_add_firmware(
  NAME
  stm32_encryptor_demo
  SRC
  ${FIRMWARE_SRC}
  LINKER_SCRIPT
  "${CMAKE_CURRENT_SOURCE_DIR}/STM32L476RG.ld"
  MCU
  STM32L476xx
  INC
  ${FIRMWARE_INC}
  LINK
  executive_mcu
  encryption_mcu
  system_core_protocols_framing_slip
  utilities_checksums_crc
  DEFS
  ${FIRMWARE_DEFS}
)

# Suppress warnings from ST vendor headers
target_include_directories(stm32_encryptor_demo SYSTEM PRIVATE ${STM32_INC})

if (APEX_USE_FREERTOS)
  target_include_directories(stm32_encryptor_demo SYSTEM PRIVATE ${FREERTOS_INC})
endif ()

# ------------------------------------------------------------------------------
# Finalize
# ------------------------------------------------------------------------------

apex_finalize_firmware()
