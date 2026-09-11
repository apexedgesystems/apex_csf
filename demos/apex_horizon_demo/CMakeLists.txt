# ==============================================================================
# apex_horizon_demo -- demo applications that stream to horizon visualizers
#
# Each subdirectory is a self-contained demo app (own executive, components,
# tprm config, and docs) that publishes simulation state over the ShmRingBridge
# for an out-of-process consumer (e.g. a visualization consumer). Demos share the
# bridge wire format but are independently buildable and runnable.
# ==============================================================================

# Shared world bundles: defined once in the bundle manifest, packed
# as tprm products, embedded by any master below via a
# `bundle <name>.world.tprm` manifest row (registration must precede
# the apps' apex_add_tprm calls, hence this position).
apex_add_bundle_tprms(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/worlds.manifest")

add_subdirectory(lidar_box)
add_subdirectory(world_query_probe)
add_subdirectory(ground_vehicle)
add_subdirectory(aircraft)
add_subdirectory(aircraft_controller)
add_subdirectory(rover_terrain/exec)
add_subdirectory(aircraft_atmo/exec)
