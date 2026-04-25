# ==============================================================================
# system_component_core - Shared concrete IComponent base
#
# Header-only library providing ComponentCore: the platform-agnostic
# concrete base for IComponent implementations. Owns identity, lifecycle,
# and registration state shared by both POSIX (SystemComponentBase) and
# MCU (LiteComponentBase) tiers.
#
# Components:
#   - ComponentCore - Concrete base with state, still abstract.
#
# Dependents:
#   - system_core_system_component (SystemComponentBase)
#   - system_component_lite (LiteComponentBase)
# ==============================================================================

include(apex/All)
apex_module(system_component_core)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_INTERFACE system_component_base
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
