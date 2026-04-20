# ==============================================================================
# Module: sim_electronics_intel4004_grid
#
# Intel 4004 transistor-level circuit assembly. Converts the parsed SPICE netlist
# (2,242 PMOS transistors, ~1,081 nets) into a Circuit object for MNA simulation.
#
# Libraries:
#   sim_electronics_intel4004_grid - Circuit assembly (header-only)
# ==============================================================================

include(apex/All)
apex_module(sim_electronics_intel4004_grid)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_electronics_circuit sim_electronics_intel4004_netlist
            sim_electronics_intel4004_behavioral sim_electronics_devices_composite
            sim_electronics_devices_nonlinear
)

# ------------------------------------------------------------------------------
# CUDA Sidecar (Optional)
# ------------------------------------------------------------------------------
# Fully GPU-resident Intel 4004 L1 NR loop. Scaffolding only at the moment:
# declares the state container and driver API. Implementation will iterate.

if (CMAKE_CUDA_COMPILER AND TARGET sim_electronics_devices_nonlinear_cuda)
  set(CUDA_SOURCES "${SRC_DIR}/Intel4004GridLevel1Cuda.cu")

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
    sim_electronics_devices_nonlinear_cuda
    sim_electronics_mna_cuda
  )

  set_target_properties(
    ${LIB_NAME}_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )

  message(STATUS "intel4004/grid: CUDA sidecar enabled")
else ()
  message(STATUS "intel4004/grid: CUDA sidecar not built")
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst dtst)
