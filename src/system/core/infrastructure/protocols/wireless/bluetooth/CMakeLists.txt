# ==============================================================================
# Module: system_core_protocols_wireless_bluetooth
#
# Bluetooth RFCOMM socket implementation for serial communication over
# Bluetooth Serial Port Profile (SPP).
#
# Libraries:
#   system_core_protocols_wireless_bluetooth - RFCOMM adapter with FD injection
# ==============================================================================

include(apex/All)
apex_module(system_core_protocols_wireless_bluetooth)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/RfcommStatus.cpp" "${SRC_DIR}/RfcommConfig.cpp"
                "${SRC_DIR}/RfcommLoopback.cpp" "${SRC_DIR}/RfcommAdapter.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PUBLIC
  utilities_compatibility
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst dtst)
