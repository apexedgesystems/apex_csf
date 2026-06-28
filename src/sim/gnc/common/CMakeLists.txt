# ==============================================================================
# Module: sim_gnc_common
#
# Vehicle-agnostic control primitives shared across vehicle classes. Header-only:
# the generic PIDLoop (any vehicle's loops compose it). Future generic estimators,
# filters, and control allocators live here too. MCU-safe (allocation-free,
# fixed-size, no exceptions), C++17-clean.
# ==============================================================================

include(apex/All)
apex_module(sim_gnc_common)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
