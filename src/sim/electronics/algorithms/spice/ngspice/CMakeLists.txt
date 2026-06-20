# ==============================================================================
# Module: sim_electronics_algorithms_spice_ngspice
#
# Wrapper for libngspice - industry-standard SPICE simulator for verification.
# Used to validate device models against golden reference implementations.
#
# Libraries:
#   sim_electronics_algorithms_spice_ngspice - ngspice wrapper
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_algorithms_spice_ngspice)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(LIB_SOURCES "${SRC_DIR}/NgspiceWrapper.cpp")

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

# Optional: Find libngspice if available
find_library(NGSPICE_LIB ngspice)
if (NGSPICE_LIB)
  set(NGSPICE_DEPS ${NGSPICE_LIB})
  set(NGSPICE_HAS 1)
  message(STATUS "Found libngspice: ${NGSPICE_LIB}")
else ()
  set(NGSPICE_DEPS "")
  set(NGSPICE_HAS 0)
  message(STATUS "libngspice not found - wrapper will use fixture-based verification only")
endif ()

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${LIB_SOURCES}
  DEPS_PUBLIC
  sim_electronics_algorithms_mna
  utilities_compatibility
  ${NGSPICE_DEPS}
)

target_compile_definitions(${LIB_NAME} PUBLIC APEX_HAS_LIBNGSPICE=${NGSPICE_HAS})

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
