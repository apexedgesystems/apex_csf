# ==============================================================================
# Module: utilities_time
#
# Time standard definitions, system clock providers, and conversion utilities
# for real-time and embedded systems. Header-only, POSIX-only.
#
# Provides TimeProviderDelegate for ATS sequencing, concrete clock functions
# (monotonic, UTC, TAI, GPS, MET), and unit/standard conversions.
# ==============================================================================

include(apex/All)
apex_module(utilities_time)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_PUBLIC utilities_concurrency)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst tst)
