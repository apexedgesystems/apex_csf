# ==============================================================================
# Performance Tests: sim_electronics_devices_nonlinear
#
# Per-device I-V evaluation and NR stamp throughput for MOSFETs, diodes, BJTs.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Nonlinear_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET Nonlinear_PTEST SOURCES ${PERF_TEST_SOURCES} LINK sim_electronics_devices_nonlinear
)

# ------------------------------------------------------------------------------
# CUDA Performance Tests
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_devices_nonlinear_cuda AND CMAKE_CUDA_COMPILER)
  set(CUDA_PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel1BatchCuda_pTest.cu")

  apex_add_ptest(
    TARGET
    MosfetLevel1BatchCuda_PTEST
    SOURCES
    ${CUDA_PERF_TEST_SOURCES}
    LINK
    sim_electronics_devices_nonlinear_cuda
    sim_electronics_devices_nonlinear
    bench_cuda
  )

  set_target_properties(
    MosfetLevel1BatchCuda_PTEST PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS
                                                                         ON
  )
endif ()
