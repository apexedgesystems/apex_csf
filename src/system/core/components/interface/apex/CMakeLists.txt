# ==============================================================================
# system_core_interface - Full-featured interface implementation
#
# External command/telemetry interface with queue-based async messaging.
#
# Components:
#   - InterfaceBase       - Abstract base with channel management
#   - ApexInterface       - SLIP/COBS framing with TCP servers
#   - ComponentQueues     - Per-component RX/TX queue management
#   - BufferPool          - Zero-copy buffer management
#   - InterfaceStatus     - Typed status codes
#
# Dependencies:
#   system_core_system_component    - Base class
#   system_core_protocols_framing_* - COBS/SLIP framing
#   system_core_protocols_network_* - TCP/UDP sockets
# ==============================================================================

include(apex/All)
apex_module(system_core_interface)

# ------------------------------------------------------------------------------
# Sources (5 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/InterfaceBase.cpp" "${SRC_DIR}/ApexInterface.cpp" "${SRC_DIR}/InterfaceStatus.cpp"
    "${SRC_DIR}/ComponentQueues.cpp" "${SRC_DIR}/BufferPool.cpp"
    "${SRC_DIR}/FileTransferHandler.cpp"
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
  system_core_system_component
  system_core_protocols_aproto
  system_core_protocols_framing_slip
  system_core_protocols_framing_cobs
  system_core_protocols_network_tcp
  system_core_protocols_network_udp
  system_core_logs
  utilities_concurrency
  utilities_compatibility
  utilities_checksums_crc
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
