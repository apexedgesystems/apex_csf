# ==============================================================================
# Module: sim_aerodynamics
#
# Body-frame aerodynamic force / moment models for atmospheric vehicles.
# Header-only: a parabolic drag-polar baseline (PolarAero), a linearized
# stability-derivative model (StabilityDerivativeAero), and an AeroModel
# fidelity ladder + wrench contributor (AeroModel.hpp). Parameterized and
# vehicle-agnostic -- a specific aircraft's coefficients come from the app.
# ==============================================================================

include(apex/All)
apex_module(sim_aerodynamics)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body # Vec3 (body-frame force/moment vectors)
            sim_dynamics_wrench # AppliedWrench / WrenchSource / WrenchKind (the contributor)
            utilities_math_vecmat # wind-axes DCM builder
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
