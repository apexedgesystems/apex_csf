# ==============================================================================
# Module: sim_environment_gravity
#
# Spherical harmonic gravity models for aerospace simulation.
# Supports Earth (EGM2008) and Moon (GRAIL) with O(N^2) RT-safe evaluation.
#
# Libraries:
#   sim_environment_gravity       - CPU implementation
#   sim_environment_gravity_cuda  - GPU acceleration (CUDA)
# ==============================================================================

include(apex/All)
apex_module(sim_environment_gravity)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/SphericalHarmonicModel.cpp" "${SRC_DIR}/GravityCoeffTable.cpp")

set(CUDA_SOURCES "${SRC_DIR}/Egm2008ModelCuda.cu")

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
  DEPS_PRIVATE
  utilities_math_legendre
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
  utilities_math_legendre_cuda
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})
apex_standard_optins(${LIB_NAME}_cuda)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
  add_subdirectory(tools)
endif ()

apex_add_test_subdirs(utst ptst dtst)
