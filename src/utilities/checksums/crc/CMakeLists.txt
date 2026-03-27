# ==============================================================================
# Module: utilities_checksums_crc
#
# Header-only CRC library with CRTP-based implementations.
# Provides table-driven, nibble-table, and bitwise strategies
# plus hardware acceleration (SSE4.2/ARM CRC32) for CRC-32C.
# ==============================================================================

include(apex/All)
apex_module(utilities_checksums_crc)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------
# Header-only library with compile-time table generation.
# Enables SSE4.2 on x86/x64 for hardware CRC-32C acceleration.
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" BAREMETAL)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[3-6]86)")
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
  if (COMPILER_SUPPORTS_SSE42)
    target_compile_options(${LIB_NAME} INTERFACE -msse4.2)
  endif ()
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst tst)
