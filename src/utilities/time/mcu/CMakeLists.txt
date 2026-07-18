# ==============================================================================
# Module: utilities_time_mcu
#
# The MCU-safe subset of the time library: time standards, the Timestamp
# type, the TimeProviderDelegate seam, and the pure standard/unit
# conversions. Freestanding-clean and BAREMETAL so firmware and hosted
# components name time the same way; the POSIX clock providers
# (SystemClocks) layer on top.
# ==============================================================================

include(apex/All)
apex_module(utilities_time_mcu)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  utilities_concurrency_mcu
  BAREMETAL
)

apex_add_floor_check(NAME ${LIB_NAME} SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/floor/FloorProbe.cpp")

apex_standard_optins(${LIB_NAME})
