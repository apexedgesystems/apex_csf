# ==============================================================================
# Domain: sim/dynamics
# ==============================================================================
# Vehicle-agnostic state-integration primitives shared across aircraft,
# rocket, and spacecraft demos.
# ==============================================================================

add_subdirectory(integrators)
add_subdirectory(rigid_body)
add_subdirectory(mass_properties)
add_subdirectory(disturbance)

# Domain-level benchmark: the step primitives are RT-path (stepped every
# tick), so one ptest exercising all four subdomains lives here. Tests are
# host-only; skip on bare-metal where the harness cannot run.
if (NOT APEX_PLATFORM_BAREMETAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ptst/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
