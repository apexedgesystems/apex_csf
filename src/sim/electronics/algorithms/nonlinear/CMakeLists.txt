# ==============================================================================
# Module: sim_electronics_algorithms_nonlinear
#
# Newton-Raphson nonlinear solver for circuits with diodes, transistors, and
# other nonlinear devices. Provides iterative operating point calculation by
# linearizing devices around current operating point and solving with MNA.
#
# Libraries:
#   sim_electronics_algorithms_nonlinear       - CPU implementation (header-only)
#   sim_electronics_algorithms_nonlinear_cuda  - GPU acceleration (future)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_algorithms_nonlinear)

# ------------------------------------------------------------------------------
# Interface Library (Header-Only)
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_mna fmt::fmt utilities_compatibility
)

# ------------------------------------------------------------------------------
# CUDA Acceleration (Optional)
# ------------------------------------------------------------------------------

if (CMAKE_CUDA_COMPILER)
  set(CUDA_SOURCES "${SRC_DIR}/NonlinearDeviceCuda.cu")

  apex_add_library(
    NAME
    ${LIB_NAME}_cuda
    TYPE
    OBJECT
    INC
    "${INC_DIR}"
    SRC
    ${CUDA_SOURCES}
    DEPS_PUBLIC
    ${LIB_NAME}
  )

  # CUDA-specific compiler options
  set_target_properties(
    ${LIB_NAME}_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )

  message(STATUS "nonlinear: CUDA acceleration enabled")
else ()
  message(STATUS "nonlinear: CUDA not available (CPU-only)")
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
