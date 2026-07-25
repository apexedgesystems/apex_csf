# ==============================================================================
# Module: sim_environment_terrain
#
# Terrain elevation models for aerospace simulation. Currently supports the
# `.htile` binary tile format (see inc/Htile.hpp for the wire spec).
# Future variants for native SRTM `.hgt` and PDS IMG+LBL would land alongside.
#
# Library:
#   sim_environment_terrain  - CPU implementation
# ==============================================================================

include(apex/All)
apex_module(sim_environment_terrain)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/Htile.cpp" "${SRC_DIR}/HtileTile.cpp")

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
  DEPS_PRIVATE
  utilities_math_vecmat
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
