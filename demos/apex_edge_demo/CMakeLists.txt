# ==============================================================================
# App: apex_edge_demo
#
# Edge compute GPU demonstration. Runs heavy CUDA workloads under
# ApexExecutive to prove RT scheduling stability under GPU saturation.
#
# Build:
#   make compose-debug              (native with CUDA)
#   cmake --preset cross-jetson-release  (cross-compile for Thor)
#
# Run:
#   ./build/hosted-x86_64-debug/bin/ApexEdgeDemo \
#     --config demos/apex_edge_demo/tprm/master.tprm --shutdown-after 10
# ==============================================================================

if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(exec)
endif ()
