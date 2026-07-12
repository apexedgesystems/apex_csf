# ==============================================================================
# apex_hil_demo firmware - STM32 flight controller
#
# HIL flight controller on NUCLEO-L476RG (Cortex-M4 @ 80 MHz).
# Receives vehicle state from host over UART/SLIP, computes control commands,
# sends thrust vector back to host plant model.
#
# Build:
#   make compose-stm32
#
# Flash:
#   make compose-stm32-flash STM32_FIRMWARE=apex_hil_demo_firmware
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# STM32CubeL4 SDK Path
# ------------------------------------------------------------------------------

set(STM32CUBE_PATH "$ENV{STM32CUBE_L4_PATH}")
if (NOT STM32CUBE_PATH)
  set(STM32CUBE_PATH "/opt/STM32CubeL4")
endif ()

# ------------------------------------------------------------------------------
# STM32 HAL Sources
# ------------------------------------------------------------------------------

set(STM32_HAL_DIR "${STM32CUBE_PATH}/Drivers/STM32L4xx_HAL_Driver")

set(STM32_HAL_SOURCES
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal.c"
    "${STM32_HAL_DIR}/Src/stm32l4xx_hal_cortex.c"
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

set(CMSIS_DIR "${STM32CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32L4xx")

set(STM32_SYSTEM_SOURCES "${CMSIS_DIR}/Source/Templates/system_stm32l4xx.c"
                         "${CMSIS_DIR}/Source/Templates/gcc/startup_stm32l476xx.s"
)

set(STM32_INC "${STM32CUBE_PATH}/Drivers/CMSIS/Include" "${CMSIS_DIR}/Include"
              "${STM32_HAL_DIR}/Inc"
)

# ------------------------------------------------------------------------------
# Application Sources (3 files)
# ------------------------------------------------------------------------------

set(APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/FlightController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cxx_stubs.cpp"
)

# ------------------------------------------------------------------------------
# Firmware Target
# ------------------------------------------------------------------------------

set(FIRMWARE_SRC ${APP_SOURCES} ${STM32_HAL_SOURCES} ${STM32_SYSTEM_SOURCES})

set(FIRMWARE_INC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${CMAKE_CURRENT_SOURCE_DIR}/../common/inc"
    "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
    "${CMAKE_SOURCE_DIR}/src/system/core/hal/stm32/inc"
)

apex_add_firmware(
  NAME
  apex_hil_demo_firmware
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
  system_core_protocols_framing_slip
  utilities_checksums_crc
)

# Suppress warnings from ST vendor headers
target_include_directories(apex_hil_demo_firmware SYSTEM PRIVATE ${STM32_INC})

# Note: apex_finalize_firmware() called by stm32_encryptor (aggregate target).
