# ==============================================================================
# Module: utilities_math_celestial
#
# Canonical celestial-body geometry and rotation constants (WGS-84 Earth,
# IAU Moon) plus the angle-conversion factors. One value per physical
# quantity, sourced; the sim's per-library constant copies migrate onto
# this leaf. Gravity-FIELD parameters stay with the gravity models.
# ==============================================================================

include(apex/All)
apex_module(utilities_math_celestial)

# lib.manifest mcu claim: pure constexpr constants, no includes at all.
apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

apex_standard_optins(${LIB_NAME})
apex_add_test_subdirs()
