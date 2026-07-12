# ==============================================================================
# apex_horizon_demo -- demo applications that stream to horizon visualizers
#
# Each subdirectory is a self-contained demo app (own executive, components,
# tprm config, and docs) that publishes simulation state over the ShmRingBridge
# for an out-of-process consumer (e.g. a UE5 horizon scene). Demos share the
# bridge wire format but are independently buildable and runnable.
# ==============================================================================

add_subdirectory(lidar_box)
