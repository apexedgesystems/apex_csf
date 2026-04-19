# ==============================================================================
# Unit Tests: sim_electronics_algorithms_nonlinear
# ==============================================================================

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_LIST_DIR}/NewtonRaphson_uTest.cpp"
                      "${CMAKE_CURRENT_LIST_DIR}/DebugIteration.cpp"
)

apex_add_gtest(
  TARGET
  sim_electronics_algorithms_nonlinear_uTest
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_algorithms_nonlinear
  LABELS
  sim
  electronics
  algorithms
)

# ------------------------------------------------------------------------------
# CUDA Tests (Optional)
# ------------------------------------------------------------------------------

if (CMAKE_CUDA_COMPILER)
  set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_LIST_DIR}/NonlinearDeviceCuda_uTest.cu")

  apex_add_gtest(
    TARGET
    sim_electronics_algorithms_nonlinear_cuda_uTest
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_electronics_algorithms_nonlinear_cuda
    LABELS
    sim
    electronics
    algorithms
    cuda
  )

  set_target_properties(
    sim_electronics_algorithms_nonlinear_cuda_uTest PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                                               CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
