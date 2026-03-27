# ==============================================================================
# Module: system_core_protocols_fieldbus_modbus
# ==============================================================================

include(apex/All)
apex_module(system_core_protocols_fieldbus_modbus)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/ModbusStatus.cpp" "${SRC_DIR}/ModbusRtuTransport.cpp"
                "${SRC_DIR}/ModbusTcpTransport.cpp" "${SRC_DIR}/ModbusMaster.cpp"
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
  system_core_protocols_serial_uart
  system_core_protocols_network_tcp
  utilities_checksums_crc
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst dtst)
