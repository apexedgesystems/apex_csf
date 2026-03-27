# ==============================================================================
# Module: encryption_lite
#
# Standalone AES-256-GCM for bare-metal targets.
# Header-only, no OpenSSL, no OS dependencies.
#
# Components:
#   - Aes256GcmLite - Software AES-256-GCM encrypt/decrypt
# ==============================================================================

include(apex/All)
apex_module(encryption_lite)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" BAREMETAL)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
