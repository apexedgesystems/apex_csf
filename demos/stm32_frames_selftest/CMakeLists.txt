# ==============================================================================
# stm32_frames_selftest - on-target golden self-test for the frames library
# ==============================================================================
#
# Runs the frames math on the NUCLEO-L476RG and compares it against the
# values the hosted suite proves on every PR: the cross-target golden-vector
# check. Verdict on USART2 (ST-Link VCP, 115200 8N1) + LED (solid = pass,
# fast blink = fail).
#
# Build:  docker compose run --rm -T dev-stm32 make stm32
# Flash:  make stm32-flash FW=stm32_frames_selftest
# ==============================================================================

include(apex/All)

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cxx_stubs.cpp"
)

set(APP_INC "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
            "${CMAKE_SOURCE_DIR}/src/system/core/hal/stm32/inc"
)

apex_add_firmware_with_stm32(
  NAME
  stm32_frames_selftest
  CUBE
  L4
  MCU
  STM32L476xx
  STARTUP
  startup_stm32l476xx
  HAL_MODULES
  cortex
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
  utilities_math_frames
)

apex_finalize_firmware()
