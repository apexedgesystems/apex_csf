# ==============================================================================
# Module: apex_hil_demo_plant
#
# Plant model and HIL components for the flight demonstration. Includes 3DOF
# dynamics, drag model, virtual flight controller, driver, and comparator.
# POSIX-only (runs on host).
# ==============================================================================

include(apex/All)
apex_module(apex_hil_demo_plant)

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/PointMassDynamics.cpp" "${SRC_DIR}/DragModel.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/../firmware/src/FlightController.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  STATIC
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PRIVATE
  sim_environment_gravity
  REQUIRES
  POSIX
)

target_include_directories(
  ${LIB_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../common/inc>
                     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../firmware/inc>
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# C2 Data Manifests (5 components)
# ------------------------------------------------------------------------------

apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plant_data.toml")
apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/vfc_data.toml")
apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../driver/driver_data.toml")
apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../support/comparator_data.toml")
apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../common/protocol_data.toml")
