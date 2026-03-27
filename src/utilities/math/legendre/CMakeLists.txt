# ==============================================================================
# Module: utilities_math_legendre
#
# High-performance library for fully normalized associated Legendre functions.
# Provides CPU and GPU computation paths with RT-safe zero-allocation APIs.
#
# Libraries:
#   utilities_math_legendre       - CPU implementation (PbarTriangle, derivatives)
#   utilities_math_legendre_cuda  - GPU acceleration (CUDA kernels, workspace API)
# ==============================================================================

include(apex/All)
apex_module(utilities_math_legendre)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/Factorials.cpp" "${SRC_DIR}/AssociatedLegendre.cpp"
                "${SRC_DIR}/PbarTriangle.cpp" "${SRC_DIR}/PbarDerivatives.cpp"
)

set(CUDA_SOURCES "${SRC_DIR}/PbarTriangleCuda.cu" "${SRC_DIR}/PbarWorkspace.cu")

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
)

# ------------------------------------------------------------------------------
# CUDA Library (requires CUDA toolkit)
# ------------------------------------------------------------------------------

apex_add_library_cuda(NAME ${LIB_NAME}_cuda CORE ${LIB_NAME} SRC ${CUDA_SOURCES})

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})
apex_standard_optins(${LIB_NAME}_cuda)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
