# ==============================================================================
# executive_watchdog - POSIX process supervisor for Apex executive applications
#
# Monitors a child executive via heartbeat pipe. Restarts on crash/hang,
# enters degraded mode after consecutive failures. No Apex runtime library
# dependencies (links only libc) to guarantee process isolation.
#
# Dependents: apps (ApexHilDemo watchdog, etc.)
# ==============================================================================

if (NOT APEX_PLATFORM_BAREMETAL)

  include(apex/All)
  apex_module(executive_watchdog)

  # ------------------------------------------------------------------------------
  # Sources (1 file)
  # ------------------------------------------------------------------------------

  set(CPP_SOURCES "${SRC_DIR}/WatchdogSupervisor.cpp")

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
  )

  # ------------------------------------------------------------------------------
  # Opt-ins (coverage, doxygen, UPX)
  # ------------------------------------------------------------------------------

  apex_standard_optins(${LIB_NAME})

endif ()
