# ==============================================================================
# Perf Tests: utilities_math_frames
# ==============================================================================

include(apex/All)

apex_add_ptest(TARGET Frames_PTEST SOURCES Frames_pTest.cpp LINK utilities_math_frames)
