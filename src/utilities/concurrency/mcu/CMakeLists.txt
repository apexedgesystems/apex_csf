# ==============================================================================
# Module: utilities_concurrency_mcu
#
# The MCU-safe subset of the concurrency library: allocation-free delegate
# types (function pointer + context). Freestanding-clean (lib.manifest) so
# firmware callbacks and the frames edge providers share one callable shape
# with the hosted components; the POSIX concurrency lib layers on top.
# ==============================================================================

include(apex/All)
apex_module(utilities_concurrency_mcu)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})
