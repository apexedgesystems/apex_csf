# ==============================================================================
# Module: sim_electronics_circuit
#
# Circuit construction and simulation API wrapper. Bridges algorithm and device
# libraries into a unified construction pattern for any circuit complexity.
#
# Libraries:
#   sim_electronics_circuit - Circuit API (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_circuit)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_algorithms_mna sim_electronics_algorithms_transient
            sim_electronics_algorithms_companions
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
