# ==============================================================================
# rover_mcu_firmware -- the rover controller on the NUCLEO-F767ZI
#
# Built by the stm32 toolchain (make compose-stm32 with
# -DAPEX_STM32_BOARD=nucleo_f767zi); flashed with
# make compose-stm32-flash STM32_FIRMWARE=rover_mcu_firmware. One board:
# the link is the ST-Link VCP (USART3), the user LEDs show lamp 1.
# ==============================================================================

include(apex/All)

if (NOT APEX_STM32_BOARD STREQUAL "nucleo_f767zi")
  message(
    STATUS "[apex] skip rover_mcu_firmware: NUCLEO-F767ZI firmware, board is '${APEX_STM32_BOARD}'"
  )
  return()
endif ()

set(ROVER_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/cxx_stubs.cpp"
    "${ROVER_BOARD_DIR}/src/RoverBoardController.cpp"
)

set(APP_INC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/system/core/hal/base"
    "${CMAKE_SOURCE_DIR}/src/system/core/hal/stm32/inc"
)

apex_add_firmware_with_stm32(
  NAME
  rover_mcu_firmware
  CUBE
  F7
  MCU
  STM32F767xx
  STARTUP
  startup_stm32f767xx
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
  "${CMAKE_CURRENT_SOURCE_DIR}/STM32F767ZI.ld"
  SRC
  ${APP_SOURCES}
  INC
  ${APP_INC}
  LINK
  executive_mcu
  system_core_protocols_framing_slip
  m # newlib libm for the guidance law's trigonometry
  DEFS
  APEX_BOARD_NUCLEO_F767ZI
)

apex_finalize_firmware()
