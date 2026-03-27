# ==============================================================================
# Module: utilities_math_integration
#
# High-performance ODE integrators with CRTP-based zero-allocation design.
# Provides explicit and implicit methods for real-time and embedded use.
#
# Libraries:
#   utilities_math_integration - Header-only integration library
# ==============================================================================

include(apex/All)
apex_module(utilities_math_integration)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")
target_link_libraries(
  ${LIB_NAME}
  INTERFACE utilities_concurrency
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
