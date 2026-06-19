# ==============================================================================
# Domain: sim/environment
# ==============================================================================
# External fields and forces acting on vehicle from surrounding environment.
# ==============================================================================

add_subdirectory(gravity)
add_subdirectory(terrain)
add_subdirectory(atmosphere)
add_subdirectory(factory)
add_subdirectory(celestial_body) # assembled body: composes the models above via the factory

# --- PORT: Uncomment as domains are ported ---
# add_subdirectory(magnetic)
# add_subdirectory(solar)
