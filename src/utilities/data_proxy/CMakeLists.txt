# ==============================================================================
# Module: utilities_data_proxy
#
# Composable data transformation proxies for byte-level operations.
#
# Provides:
#   - ByteMaskProxy (general-purpose AND/XOR mask queue)
#   - EndiannessProxy (byte-swap support)
#   - MasterDataProxy (convenience compositor for proxy chains)
#
# Design: All RT-safe, no dynamic allocation, trivially copyable types only.
# Proxies are infrastructure primitives that support components compose into
# their own transformation stacks.
#
# Libraries:
#   utilities_data_proxy - Header-only data proxy library
# ==============================================================================

include(apex/All)
apex_module(utilities_data_proxy)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_INTERFACE utilities_compatibility)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${TST_DIR}/CMakeLists.txt")
  add_subdirectory(tst)
endif ()

if (EXISTS "${UTST_DIR}/CMakeLists.txt")
  add_subdirectory(utst)
endif ()

if (EXISTS "${PTST_DIR}/CMakeLists.txt")
  add_subdirectory(ptst)
endif ()
