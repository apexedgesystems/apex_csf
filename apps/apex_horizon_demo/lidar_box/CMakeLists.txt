# ==============================================================================
# lidar_box -- body-in-a-box clearance-lidar demo
#
# A body drifts through a fixed asymmetric box on a 3D Lissajous path while a
# six-axis clearance lidar measures the distance to each wall. The producer
# publishes a 48-byte LidarBoxFrame (pose + six clearances) that a ShmRingBridge
# streams to /lidar_box at 50 Hz for an out-of-process visualizer.
# ==============================================================================

add_subdirectory(producer)
add_subdirectory(exec)
