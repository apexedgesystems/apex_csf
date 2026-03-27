# ==============================================================================
# App: apex_edge_demo
#
# Edge compute GPU demonstration. Runs heavy CUDA workloads under
# ApexExecutive to prove RT scheduling stability under GPU saturation.
#
# Build:
#   make compose-debug              (native with CUDA)
#   cmake --preset jetson-aarch64-release  (cross-compile for Thor)
#
# Run:
#   ./build/native-linux-debug/bin/ApexEdgeDemo \
#     --config apps/apex_edge_demo/tprm/master.tprm --shutdown-after 10
# ==============================================================================

if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(exec)
endif ()
