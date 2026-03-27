# ==============================================================================
# Simulation Models
# ==============================================================================
# Models are added as domains are ported.
# ==============================================================================

# ------------------------------------------------------------------------------
# Active Domains
# ------------------------------------------------------------------------------

add_subdirectory(gpu_compute)
add_subdirectory(analog)
add_subdirectory(environment)

# --- PORT: Uncomment as domains are ported ---
# add_subdirectory(kinematics)
# add_subdirectory(apex_models)

# --- PORT: Planned domains (not yet implemented) ---
# add_subdirectory(frames)
# add_subdirectory(dynamics)
# add_subdirectory(aero)
# add_subdirectory(propulsion)
# add_subdirectory(hardware)
