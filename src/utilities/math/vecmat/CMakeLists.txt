# ==============================================================================
# Module: utilities_math_vecmat
#
# Fixed-size 3-vector / 3x3-matrix / rotation operations over raw arrays.
# The MCU-clean small-algebra tier: header-only, allocation-free, no BLAS --
# the home for the cross/dot/DCM math the sim libraries carry locally, and
# the substrate the frames library composes.
# ==============================================================================

include(apex/All)
apex_module(utilities_math_vecmat)

# BAREMETAL: freestanding-clean headers (compat math shims only), so firmware
# links the same small algebra the sim uses.
apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  utilities_compatibility
  BAREMETAL
)

apex_standard_optins(${LIB_NAME})
apex_add_test_subdirs()
