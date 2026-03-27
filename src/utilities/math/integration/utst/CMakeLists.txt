# ==============================================================================
# Unit Tests: utilities_math_integration
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Test Sources (16 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    ExplicitEuler_uTest.cpp
    RungeKutta4_uTest.cpp
    BackwardEuler_uTest.cpp
    TrapezoidalRule_uTest.cpp
    BDF2_uTest.cpp
    AdamsBashforth_uTest.cpp
    AdamsMoulton_uTest.cpp
    SDIRK2_uTest.cpp
    StateVector_uTest.cpp
    Accumulator_uTest.cpp
    Leapfrog_uTest.cpp
    RungeKutta45_uTest.cpp
    Quaternion_uTest.cpp
    BDF_uTest.cpp
    Rosenbrock_uTest.cpp
    RungeKuttaNystrom_uTest.cpp
)

# ------------------------------------------------------------------------------
# Test Executable
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestMathIntegration
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  ${LIB_NAME}
  LABELS
  math
  integration
)
