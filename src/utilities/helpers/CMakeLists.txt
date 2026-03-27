# ==============================================================================
# Module: utilities_helpers
#
# Low-level helper utilities for embedded and real-time systems.
# Provides bit manipulation, byte ordering, CPU primitives, file descriptor
# management, and socket utilities with RT-safety annotations.
# ==============================================================================

include(apex/All)
apex_module(utilities_helpers)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_PUBLIC fmt::fmt)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst tst)
