# ==============================================================================
# Perf Tests: utilities_math_vecmat
# ==============================================================================

include(apex/All)

apex_add_ptest(TARGET Vecmat_PTEST SOURCES Vecmat_pTest.cpp LINK utilities_math_vecmat)
