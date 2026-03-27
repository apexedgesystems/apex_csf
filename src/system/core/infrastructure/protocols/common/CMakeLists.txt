# ==============================================================================
# Module: system_core_protocols_common
# Description: Common utilities for protocol libraries (header-only)
# ==============================================================================

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_module(system_core_protocols_common)

add_library(system_core_protocols_common INTERFACE)
add_library(apex::system_core_protocols_common ALIAS system_core_protocols_common)

target_include_directories(system_core_protocols_common INTERFACE ${PROJECT_SOURCE_DIR})

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(system_core_protocols_common)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
