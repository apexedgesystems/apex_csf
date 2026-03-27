# ==============================================================================
# Module: hal_stm32
#
# STM32-specific HAL implementations.
# Requires STM32 HAL libraries (from CubeMX or manual setup).
#
# Libraries:
#   hal_stm32 - STM32 UART, SPI, etc. implementations (header-only)
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library (header-only, requires STM32 HAL at link time)
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  hal_stm32
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  DEPS_INTERFACE
  hal_interface
  BAREMETAL
)

# Additional include for src/ path resolution
target_include_directories(hal_stm32 INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(hal_stm32)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Unit tests (mock mode, host-side)
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()

# User must link STM32 HAL and set STM32L4xx/STM32F4xx define
# This is typically done in the application CMakeLists.txt:
#   target_compile_definitions(my_app PRIVATE STM32L4xx)
#   target_link_libraries(my_app PRIVATE stm32l4_hal hal_stm32)
