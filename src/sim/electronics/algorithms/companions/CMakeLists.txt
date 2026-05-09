# ==============================================================================
# Module: sim_electronics_algorithms_companions
#
# Companion models for reactive elements (capacitors, inductors) in transient
# simulation. Provides device-specific integration wrappers around Layer 1
# integration algorithms (Backward Euler, Trapezoidal, GEAR2).
#
# Companion models discretize differential equations (I=C*dV/dt, V=L*dI/dt)
# into equivalent resistor + current source circuits that can be stamped into
# the MNA matrix at each time step.
#
# Libraries:
#   sim_electronics_algorithms_companions       - Companion models (header-only)
#   sim_electronics_algorithms_companions_cuda  - GPU batch evaluation
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_algorithms_companions)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CUDA_SOURCES "${SRC_DIR}/CompanionSetCuda.cu")

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_algorithms_mna # MNA stamping (MnaSystem, MnaSystemSparse)
            sim_electronics_algorithms_transient # Integration methods (IntegrationMethod enum)
            utilities_compatibility # C++17/20/23 compatibility
)

# ------------------------------------------------------------------------------
# CUDA Library (requires CUDA toolkit)
# ------------------------------------------------------------------------------

apex_add_library_cuda(
  NAME
  ${LIB_NAME}_cuda
  CORE
  ${LIB_NAME}
  SRC
  ${CUDA_SOURCES}
  DEPS_PRIVATE
  sim_electronics_algorithms_mna_cuda
)
apex_standard_optins(${LIB_NAME}_cuda)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
