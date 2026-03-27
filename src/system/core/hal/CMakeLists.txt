# ==============================================================================
# hal - Hardware Abstraction Layer
#
# Directory structure:
#   base/  - Platform-agnostic interfaces (IUart, ICan, ISpi, II2c, IFlash)
#   stm32/ - STM32 implementations (requires STM32 HAL)
#   avr/   - AVR implementations (ATmega328P, avr-libc)
#   pico/  - Raspberry Pi Pico implementations (Pico SDK)
#   esp32/ - ESP32 implementations (ESP-IDF)
#
# Libraries:
#   hal_interface - Abstract interfaces (INTERFACE, header-only)
#   hal_stm32     - STM32 implementations (INTERFACE, header-only)
#   hal_avr       - AVR implementations (INTERFACE, header-only)
#   hal_pico      - Pico implementations (INTERFACE, header-only)
#   hal_esp32     - ESP32 implementations (INTERFACE, header-only)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Base interfaces (always built)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/base/CMakeLists.txt")
  add_subdirectory(base)
endif ()

# Platform implementations: always added so mock-mode tests run on host.
# Real hardware code is gated by mock defines inside each header.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stm32/CMakeLists.txt")
  add_subdirectory(stm32)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/avr/CMakeLists.txt")
  add_subdirectory(avr)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pico/CMakeLists.txt")
  add_subdirectory(pico)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/esp32/CMakeLists.txt")
  add_subdirectory(esp32)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/c2000/CMakeLists.txt")
  add_subdirectory(c2000)
endif ()
