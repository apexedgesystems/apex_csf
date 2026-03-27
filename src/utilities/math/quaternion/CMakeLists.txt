# ==============================================================================
# Module: utilities_math_quaternion
#
# Quaternion library for 3D rotations. Non-owning views with RT-safe operations.
#
# Libraries:
#   utilities_math_quaternion      - Header-only quaternion operations
#   utilities_math_quaternion_cuda - GPU acceleration for batch operations
# ==============================================================================

include(apex/All)
apex_module(utilities_math_quaternion)

# ------------------------------------------------------------------------------
# CUDA Sources
# ------------------------------------------------------------------------------

set(CUDA_SOURCES "${SRC_DIR}/QuaternionCuda.cu")

# ------------------------------------------------------------------------------
# Interface Library (header-only CPU)
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

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
