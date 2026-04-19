# ==============================================================================
# Tests: sim_electronics_algorithms_transient
# ==============================================================================

include(apex/All)

set(TEST_SOURCES Transient_uTest.cpp)

apex_add_gtest(
  TARGET
  TestSimElectronicsAlgorithmsTransient
  SOURCES
  ${TEST_SOURCES}
  LINK
  sim_electronics_algorithms_transient
  LABELS
  sim
  electronics
  algorithms
  transient
)

# ------------------------------------------------------------------------------
# CUDA Tests (Optional)
# ------------------------------------------------------------------------------

if (CMAKE_CUDA_COMPILER)
  set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_LIST_DIR}/TransientCuda_uTest.cu")

  apex_add_gtest(
    TARGET
    sim_electronics_algorithms_transient_cuda_uTest
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_electronics_algorithms_transient_cuda
    sim_electronics_algorithms_transient
    LABELS
    sim
    electronics
    algorithms
    transient
    cuda
  )

  set_target_properties(
    sim_electronics_algorithms_transient_cuda_uTest PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                                               CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
