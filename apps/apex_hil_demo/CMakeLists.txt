# ==============================================================================
# App: apex_hil_demo
#
# HIL flight demonstration. Routes to firmware/ (STM32) or model/+exec/ (POSIX)
# based on build platform. Both sides run simultaneously: the POSIX host runs
# the plant model, while the STM32 runs the flight controller over UART/SLIP.
#
# POSIX (host plant + orchestrator):
#   make compose-debug
#
# STM32 (flight controller firmware):
#   make compose-stm32
# ==============================================================================

if (APEX_PLATFORM_BAREMETAL AND APEX_HAL_PLATFORM STREQUAL "stm32")
  # STM32: Build flight controller firmware only
  add_subdirectory(firmware)
elseif (NOT APEX_PLATFORM_BAREMETAL)
  # POSIX: Build plant model library + orchestrator executable + watchdog
  add_subdirectory(model)
  add_subdirectory(exec)
  add_subdirectory(watchdog)
  add_subdirectory(test/plugin)
endif ()
# Other bare-metal platforms: skip (HIL demo only supports STM32 firmware)
