# ==============================================================================
# Tests: sim_electronics_algorithms_companions
# ==============================================================================

include(apex/All)

set(UNIT_TEST_SOURCES CompanionModels_uTest.cpp CompanionSet_uTest.cpp)

apex_add_gtest(
  TARGET
  TestSimElectronicsAlgorithmsCompanions
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_algorithms_companions
  LABELS
  sim
  electronics
  algorithms
  companions
)

# ------------------------------------------------------------------------------
# CUDA Tests (requires CUDA toolkit + sim_electronics_algorithms_companions_cuda)
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_algorithms_companions_cuda AND CMAKE_CUDA_COMPILER)
  apex_add_gtest(
    TARGET
    TestSimElectronicsAlgorithmsCompanionsCuda
    SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CompanionSetCuda_uTest.cu"
    LINK
    sim_electronics_algorithms_companions_cuda
    sim_electronics_algorithms_companions
    LABELS
    sim
    electronics
    algorithms
    companions
    cuda
  )
  set_target_properties(
    TestSimElectronicsAlgorithmsCompanionsCuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                                          CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
