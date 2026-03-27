# ==============================================================================
# Module: system_core_protocols_aproto
# ==============================================================================
# APROTO packet protocol for command/telemetry interface.
#
# Components:
# - AprotoTypes   - Header and payload structures (14-byte fixed header)
# - AprotoStatus  - Typed status codes
# - AprotoCodec   - Encode/decode functions with optional CRC32
# - AprotoProcessor - RT-safe streaming packet extractor
# - AprotoMutableMessage - Typed facade for packet assembly
#
# Dependencies:
#   utilities_checksums_crc - Hardware-accelerated CRC32 for packet validation
#
# Libraries:
#   system_core_protocols_aproto - APROTO codec
# ==============================================================================

include(apex/All)
apex_module(system_core_protocols_aproto)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/AprotoStatus.cpp" "${SRC_DIR}/AprotoCodec.cpp"
                "${SRC_DIR}/AprotoProcessor.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PUBLIC
  utilities_checksums_crc
)

# ------------------------------------------------------------------------------
# Data Definition (struct dictionary for C2)
# ------------------------------------------------------------------------------

apex_add_data_manifest()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
