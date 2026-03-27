# ==============================================================================
# Module: utilities_math_linalg
#
# Linear algebra library providing non-owning array views with BLAS/LAPACK
# acceleration. Zero-allocation core for RT-safe operations.
#
# Libraries:
#   utilities_math_linalg      - CPU implementation (ArrayBase, Array, Vector, Matrix3)
#   utilities_math_linalg_cuda - GPU acceleration for batch operations
# ==============================================================================

include(apex/All)
apex_module(utilities_math_linalg)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/Array.cpp")

set(CUDA_SOURCES "${SRC_DIR}/ArrayCuda.cu")

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

# Link BLAS/LAPACK if available
# No hardcoded paths - let CMake use CMAKE_LIBRARY_PATH from toolchain
find_library(CBLAS_LIB NAMES cblas blas)
find_library(LAPACKE_LIB NAMES lapacke)
find_library(LAPACK_LIB NAMES lapack)

if (CBLAS_LIB)
  target_link_libraries(${LIB_NAME} PRIVATE ${CBLAS_LIB})
  message(STATUS "linalg: linking CBLAS from ${CBLAS_LIB}")
endif ()

if (LAPACKE_LIB)
  target_link_libraries(${LIB_NAME} PRIVATE ${LAPACKE_LIB})
  message(STATUS "linalg: linking LAPACKE from ${LAPACKE_LIB}")
endif ()

if (LAPACK_LIB)
  target_link_libraries(${LIB_NAME} PRIVATE ${LAPACK_LIB})
  message(STATUS "linalg: linking LAPACK from ${LAPACK_LIB}")
endif ()

# Link to compatibility library for compat_blas.hpp
target_link_libraries(
  ${LIB_NAME}
  PUBLIC utilities_compatibility
)

# ------------------------------------------------------------------------------
# CUDA Library
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
