# ==============================================================================
# Module: sim_electronics_algorithms_transient
#
# Time-domain circuit simulation engine for reactive elements (capacitors,
# inductors). Interfaces with MNA solvers for each timestep using backward
# Euler or trapezoidal integration.
#
# Libraries:
#   sim_electronics_algorithms_transient       - CPU implementation (header-only)
#   sim_electronics_algorithms_transient_cuda  - GPU acceleration (CUDA kernels)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_algorithms_transient)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CUDA_SOURCES "${SRC_DIR}/TransientCuda.cu")

# ------------------------------------------------------------------------------
# Interface Library (Header-Only)
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_mna
            sim_electronics_devices_companions # Companion models (Layer 2 dependency)
            fmt::fmt utilities_compatibility
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
  sim_electronics_mna_cuda
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
