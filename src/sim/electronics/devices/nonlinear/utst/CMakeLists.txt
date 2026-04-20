# ==============================================================================
# Unit Tests: sim_electronics_devices_nonlinear
# ==============================================================================

set(TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BjtEbersMoll_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DiodeShockley_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DiodeSpice_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JfetLevel2_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JfetShichman_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetBinarySwitch_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel1_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel2_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel3_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SchottkyDiode_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ZenerDiode_uTest.cpp"
)

apex_add_gtest(
  TARGET
  sim_electronics_devices_nonlinear_uTest
  SOURCES
  ${TEST_SOURCES}
  LINK
  sim_electronics_devices_nonlinear
  COVERAGE_FOR
  sim_electronics_devices_nonlinear
  LABELS
  devices
)

# ------------------------------------------------------------------------------
# CUDA Tests (requires CUDA toolkit + sim_electronics_devices_nonlinear_cuda)
# ------------------------------------------------------------------------------

if (TARGET sim_electronics_devices_nonlinear_cuda AND CMAKE_CUDA_COMPILER)
  set(CUDA_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/MosfetLevel1BatchCuda_uTest.cu")

  apex_add_gtest(
    TARGET
    sim_electronics_devices_nonlinear_cuda_uTest
    SOURCES
    ${CUDA_TEST_SOURCES}
    LINK
    sim_electronics_devices_nonlinear_cuda
    sim_electronics_devices_nonlinear
    LABELS
    devices
    cuda
  )

  set_target_properties(
    sim_electronics_devices_nonlinear_cuda_uTest PROPERTIES CUDA_SEPARABLE_COMPILATION ON
                                                            CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif ()
