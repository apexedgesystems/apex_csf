# ==============================================================================
# executive_core - Shared concrete IExecutive base
#
# Header-only library providing ExecutiveCore: the platform-agnostic
# concrete base for IExecutive implementations. Defines executive
# identity constants (id=0, name="Executive", type=EXECUTIVE) and
# preserves the IExecutive contract as pure virtual.
#
# Components:
#   - ExecutiveCore - Concrete base with identity constants, abstract.
#
# Dependents:
#   - system_core_executive (ExecutiveBase / ApexExecutive)
#   - executive_lite (LiteExecutive)
# ==============================================================================

include(apex/All)
apex_module(executive_core)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(
  NAME
  ${LIB_NAME}
  INC
  "${INC_DIR}"
  DEPS_INTERFACE
  executive_base
  system_component_base
  BAREMETAL
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
