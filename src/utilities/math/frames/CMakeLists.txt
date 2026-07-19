# ==============================================================================
# Module: utilities_math_frames
#
# Reference frames and rigid transforms: the SE(3) Transform POD, the frame
# graph, and the standard celestial catalog. Tier S: composes quaternion +
# vecmat + celestial + the concurrency mcu delegate and nothing else, so the
# same frame math runs in the sim (double) and on MCU control paths (float).
# ==============================================================================

include(apex/All)
apex_module(utilities_math_frames)

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  utilities_compatibility
  utilities_math_quaternion
  utilities_math_vecmat
  utilities_math_celestial
  utilities_concurrency_mcu
  BAREMETAL
)

apex_standard_optins(${LIB_NAME})
apex_add_test_subdirs()
