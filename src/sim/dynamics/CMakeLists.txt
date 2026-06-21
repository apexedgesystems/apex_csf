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
add_subdirectory(vehicle) # assembled vehicle: composes the modules above and steps the 6-DOF
