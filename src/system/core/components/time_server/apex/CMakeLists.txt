# ==============================================================================
# system_core_time_server - PPS-driven UTC distribution
#
# Sole authority for wall-clock time in the system. Ingests a PPS signal
# via the IPps HAL, optionally pairs it with an external UTC reference,
# correlates the local steady clock to UTC, and broadcasts the
# TimeAtNextTone (TNT) message at 1 Hz.
#
# Dependents: executive (registers as a core component)
# ==============================================================================

include(apex/All)
apex_module(system_core_time_server)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/TimeServer.cpp" "${SRC_DIR}/TimeServerData.cpp")

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
  hal_interface
  system_core_system_component
  utilities_concurrency
  utilities_time
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
