# ==============================================================================
# Module: sim_aerodynamics
#
# Body-frame aerodynamic force / moment models for atmospheric vehicles.
# Header-only: a parabolic drag-polar baseline (PolarAero) and a linearized
# stability-derivative model (StabilityDerivativeAero) that produces body-frame
# forces and moments. Parameterized and vehicle-agnostic -- a specific
# aircraft's coefficients are supplied by the application.
# ==============================================================================

include(apex/All)
apex_module(sim_aerodynamics)

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}")

target_link_libraries(
  ${LIB_NAME}
  INTERFACE sim_dynamics_rigid_body # Vec3 (body-frame force/moment vectors)
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs(utst ptst)
