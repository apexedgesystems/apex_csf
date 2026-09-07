# ==============================================================================
# Module: sim_environment_world
#
# World bundle container: one packed, hash-identified binary per world
# (<body>.world.tprm) collecting the content artifacts that construct a
# simulated body -- gravity table, terrain tiling, atmosphere table.
# Content sibling of the component tprm family: same identity
# discipline (magic/version/uid/hash), bulk-sized container (64-bit
# offsets, in-place entry access, lazy per-entry verification).
#
# Library:
#   sim_environment_world - reader/writer + format definitions
#
# Mirrors sim_environment_atmosphere organization. See README.md.
# ==============================================================================

include(apex/All)
apex_module(sim_environment_world)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/WorldBundle.cpp")

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
  utilities_checksums_crc
)

# ------------------------------------------------------------------------------
# Packer/verifier tool (callable standalone; producers use it as a
# black box, packaging drives it through apex_add_world)
# ------------------------------------------------------------------------------

apex_add_app(
  NAME
  world_pack
  SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/tool/WorldPack.cpp"
  LINK
  ${LIB_NAME}
  sim_environment_atmosphere
  sim_environment_terrain
  NO_INSTALL
  NO_UPX
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
