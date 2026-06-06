# ==============================================================================
# Module: sim_electronics_algorithms_mna
#
# Modified Nodal Analysis (MNA) systems for circuit simulation.
# Provides optimized dense (BLAS/LAPACK) and sparse (KLU) solvers with
# 25x sparse and 31x dense speedup over baseline.
#
# Dependencies: BLAS, LAPACK, SuiteSparse (KLU)
# RT-safety: Core MNA operations RT-safe with pre-allocated workspace
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_algorithms_mna)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/MnaSystemSparse.cpp")

set(CUDA_SOURCES "${SRC_DIR}/MnaSystemCuda.cu" "${SRC_DIR}/MnaBatchCuda.cu")

# ------------------------------------------------------------------------------
# External Dependencies
# ------------------------------------------------------------------------------

# LAPACK and BLAS for dense MNA solver
find_library(LAPACKE_LIB NAMES lapacke)
find_library(LAPACK_LIB NAMES lapack)
find_library(BLAS_LIB NAMES openblas blas)

# KLU sparse solver (SuiteSparse)
find_library(KLU_LIB NAMES klu)
find_library(AMD_LIB NAMES amd)
find_library(COLAMD_LIB NAMES colamd)
find_library(BTF_LIB NAMES btf)
find_library(SUITESPARSE_CONFIG_LIB NAMES suitesparseconfig)
find_path(
  SUITESPARSE_INCLUDE_DIR
  NAMES klu.h
  PATH_SUFFIXES suitesparse
)

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
  DEPS_PUBLIC
  fmt::fmt
  utilities_compatibility
)

# Link LAPACK/BLAS (PUBLIC because MnaSystem::solve() is header-only)
if (LAPACKE_LIB)
  target_link_libraries(${LIB_NAME} PUBLIC ${LAPACKE_LIB})
  message(STATUS "electronics/mna: linking LAPACKE from ${LAPACKE_LIB}")
endif ()

if (LAPACK_LIB)
  target_link_libraries(${LIB_NAME} PUBLIC ${LAPACK_LIB})
  message(STATUS "electronics/mna: linking LAPACK from ${LAPACK_LIB}")
endif ()

if (BLAS_LIB)
  target_link_libraries(${LIB_NAME} PUBLIC ${BLAS_LIB})
  message(STATUS "electronics/mna: linking BLAS from ${BLAS_LIB}")
endif ()

# Link KLU sparse solver (required)
if (NOT KLU_LIB OR NOT SUITESPARSE_INCLUDE_DIR)
  message(FATAL_ERROR "electronics/mna: KLU (SuiteSparse) is required. Install libsuitesparse-dev.")
endif ()

target_link_libraries(
  ${LIB_NAME}
  PUBLIC ${KLU_LIB} ${AMD_LIB} ${COLAMD_LIB} ${BTF_LIB} ${SUITESPARSE_CONFIG_LIB}
)
target_include_directories(${LIB_NAME} PUBLIC ${SUITESPARSE_INCLUDE_DIR})
message(STATUS "electronics/mna: linking KLU from ${KLU_LIB}")

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
  NO_NVML
)

if (CMAKE_CROSSCOMPILING)
  find_library(
    _CUSOLVER_CROSS_LIB
    NAMES cusolver
    PATHS /usr/local/cuda/targets/sbsa-linux/lib/stubs
          /usr/local/cuda/targets/aarch64-linux/lib/stubs
    NO_DEFAULT_PATH
  )
  if (_CUSOLVER_CROSS_LIB)
    target_link_libraries(${LIB_NAME}_cuda PRIVATE ${_CUSOLVER_CROSS_LIB})
  endif ()
elseif (TARGET CUDA::cusolver)
  target_link_libraries(${LIB_NAME}_cuda PRIVATE CUDA::cusolver)
endif ()
apex_standard_optins(${LIB_NAME}_cuda)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
