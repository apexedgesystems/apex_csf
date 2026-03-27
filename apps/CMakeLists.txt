# ==============================================================================
# apps/CMakeLists.txt - Application targets
# ==============================================================================

# ------------------------------------------------------------------------------
# Linux Applications
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
# Edge / GPU / Analysis Applications
# ------------------------------------------------------------------------------

add_subdirectory(apex_edge_demo)
add_subdirectory(apex_mc_demo)

add_subdirectory(apex_hil_demo)

# ------------------------------------------------------------------------------
# Bare-metal Firmware
# ------------------------------------------------------------------------------

if (APEX_PLATFORM_BAREMETAL)
  if (APEX_HAL_PLATFORM STREQUAL "stm32")
    add_subdirectory(stm32_encryptor_demo)
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
