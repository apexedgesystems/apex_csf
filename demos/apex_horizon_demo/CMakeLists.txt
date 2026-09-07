# ==============================================================================
# apex_horizon_demo -- demo applications that stream to horizon visualizers
#
# Each subdirectory is a self-contained demo app (own executive, components,
# tprm config, and docs) that publishes simulation state over the ShmRingBridge
# for an out-of-process consumer (e.g. a visualization consumer). Demos share the
# bridge wire format but are independently buildable and runnable.
# ==============================================================================

# Shared worlds: one bundle per body, referenced by any deployment
# below (WORLD earth). The atmosphere table is the canonical source;
# the terrain entry joins when the regenerated artifact lands.
apex_add_world(
  BODY earth COMPONENT_ID 0x0101 ATMOSPHERE
  demos/apex_horizon_demo/aircraft_atmo/data/earth_ussa76.atm
)

add_subdirectory(lidar_box)
add_subdirectory(world_query_probe)
add_subdirectory(ground_vehicle)
add_subdirectory(aircraft)
add_subdirectory(aircraft_controller)
add_subdirectory(rover_terrain/exec)
add_subdirectory(aircraft_atmo/exec)
