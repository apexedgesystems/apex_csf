# ==============================================================================
# ApexEdgeDemo release manifest
#
# Edge compute GPU demonstration:
#   Thor:  GPU workload models + RT executive (POSIX executable, CUDA)
# ==============================================================================

APP_REGISTRY += ApexEdgeDemo

APP_ApexEdgeDemo_PLATFORMS          := jetson
APP_ApexEdgeDemo_TPRM               := apps/apex_edge_demo/tprm/master_thor.tprm
APP_ApexEdgeDemo_jetson_TYPE        := posix
APP_ApexEdgeDemo_jetson_BINARY      := ApexEdgeDemo
