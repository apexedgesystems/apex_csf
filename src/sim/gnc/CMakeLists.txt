# ==============================================================================
# Guidance, navigation, and control (gnc)
#
# Organized by vehicle class so each class's algorithms are separable:
#
#   common/    -- vehicle-agnostic primitives (PIDLoop, future estimators/filters)
#   aircraft/  -- aircraft autopilot loops (atmospheric flight, aero surfaces)
#
# Additional vehicle classes (spacecraft ADCS, rover path-following, ...) slot in
# as sibling subdirectories that compose the common primitives.
# ==============================================================================

add_subdirectory(common)
add_subdirectory(aircraft)
