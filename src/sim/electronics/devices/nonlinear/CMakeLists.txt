# ==============================================================================
# Module: sim_electronics_devices_nonlinear
#
# Nonlinear device models (diode, MOSFET, BJT). Provides physics engines for
# devices with nonlinear I-V characteristics, using Layer 1 Newton-Raphson
# solver for circuit simulation.
#
# Libraries:
#   sim_electronics_devices_nonlinear       - Nonlinear device models (header-only)
#   sim_electronics_devices_nonlinear_cuda  - GPU acceleration (future)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_devices_nonlinear)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_algorithms_mna # MNA stamping (MnaSystem)
            sim_electronics_algorithms_nonlinear # Newton-Raphson solver
            utilities_compatibility # C++17/20/23 compatibility
)

# ------------------------------------------------------------------------------
# CUDA Library (requires CUDA toolkit)
# ------------------------------------------------------------------------------

if (CMAKE_CUDA_COMPILER)
  set(CUDA_SOURCES "${SRC_DIR}/MosfetLevel1BatchCuda.cu")

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

  set_target_properties(
    ${LIB_NAME}_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )

  message(STATUS "devices/nonlinear: CUDA acceleration enabled")
else ()
  message(STATUS "devices/nonlinear: CUDA not available (CPU-only)")
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
