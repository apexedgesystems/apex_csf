# ==============================================================================
# Unit Tests: sim_electronics_algorithms_mna
#
# Dense (LAPACK) and sparse (KLU) MNA solver verification. Tests cover
# voltage dividers, current injection, parallel resistors, voltage sources,
# AC frequency sweep, and GPU batch solve.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (2 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MnaSystemSparse_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MnaSystem_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/AcMnaSystem_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/StampContext_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestSimElectronicsMna
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  sim_electronics_algorithms_mna
  LABELS
  sim
  electronics
  mna
)

# ------------------------------------------------------------------------------
# CUDA Tests (requires CUDA toolkit)
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_algorithms_mna_cuda AND CMAKE_CUDA_COMPILER)
  set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/MnaBatchCuda_uTest.cu")

  apex_add_gtest(
    TARGET
    TestSimElectronicsMnaBatch
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_electronics_algorithms_mna_cuda
    LABELS
    sim
    electronics
    mna
    cuda
  )
endif ()
