# ==============================================================================
# demos/CMakeLists.txt - Stock demo applications
#
# Every directory here is an apex-maintained example. User production
# applications live under apps/ (see apps/README.md).
# ==============================================================================

# ------------------------------------------------------------------------------
# Edge / GPU / Analysis Applications
# ------------------------------------------------------------------------------

add_subdirectory(apex_edge_demo)
add_subdirectory(apex_mc_demo)

add_subdirectory(apex_action_demo)
add_subdirectory(apex_hil_demo)
add_subdirectory(apex_horizon_demo)
add_subdirectory(apex_ops_demo)
add_subdirectory(apex_time_demo)

# ------------------------------------------------------------------------------
# Electronics Simulation Demos
# ------------------------------------------------------------------------------

add_subdirectory(apex_cpu_sim_demo)
add_subdirectory(apex_circuit_demo)

# ------------------------------------------------------------------------------
# Bare-metal Firmware
# ------------------------------------------------------------------------------

if (APEX_PLATFORM_BAREMETAL)
  if (APEX_HAL_PLATFORM STREQUAL "stm32")
    add_subdirectory(stm32_encryptor_demo)
    add_subdirectory(stm32_frames_selftest)
  elseif (APEX_HAL_PLATFORM STREQUAL "avr")
    add_subdirectory(arduino_encryptor_demo)
  elseif (APEX_HAL_PLATFORM STREQUAL "pico")
    add_subdirectory(pico_encryptor_demo)
  elseif (APEX_HAL_PLATFORM STREQUAL "esp32")
    add_subdirectory(esp32_encryptor_demo)
  elseif (APEX_HAL_PLATFORM STREQUAL "c2000")
    add_subdirectory(c2000_encryptor_demo)
  endif ()
endif ()
