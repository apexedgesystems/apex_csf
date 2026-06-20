# ==============================================================================
# Domain: sim/dynamics
# ==============================================================================
# Vehicle-agnostic state-integration primitives shared across aircraft,
# rocket, and spacecraft demos.
# ==============================================================================

add_subdirectory(integrators)
add_subdirectory(rigid_body)
add_subdirectory(mass_properties)
add_subdirectory(force_moment)
add_subdirectory(disturbance)

# ------------------------------------------------------------------------------
# Domain-level facade: sim_dynamics
#
# VehicleStep.hpp ties the three composition layers together (it includes
# rigid_body + mass_properties + force_moment) and offers a stepRigidBody6DOF
# overload that consumes the aggregates directly. It lives above the leaf
# modules to keep rigid_body free of any dependency on mass_properties /
# force_moment (one-directional includes, no cycles).
# ------------------------------------------------------------------------------
include(apex/All)
apex_module(sim_dynamics)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body sim_dynamics_mass_properties sim_dynamics_force_moment
)

apex_standard_optins(${LIB_NAME})

# Domain-level benchmark: the step primitives are RT-path (stepped every
# tick), so one ptest exercising all four subdomains lives here. Tests are
# host-only; skip on bare-metal where the harness cannot run.
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ptst/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
