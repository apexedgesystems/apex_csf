# ==============================================================================
# Module: utilities_encryption
#
# Cryptographic primitives for real-time and embedded systems.
# Provides hashing, MAC, symmetric ciphers, and AEAD with zero-allocation APIs.
#
# Dependencies: OpenSSL (libssl, libcrypto)
# Requires: POSIX (not available on bare-metal)
# ==============================================================================

include(apex/All)
apex_module(utilities_encryption)

# ------------------------------------------------------------------------------
# Sources (18 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/EncryptionUtilities.cpp"
    "${SRC_DIR}/Sha256Hash.cpp"
    "${SRC_DIR}/Sha512Hash.cpp"
    "${SRC_DIR}/Md5Hash.cpp"
    "${SRC_DIR}/Blake2sHash.cpp"
    "${SRC_DIR}/Sha3Hash256.cpp"
    "${SRC_DIR}/HmacSha256.cpp"
    "${SRC_DIR}/HmacSha512.cpp"
    "${SRC_DIR}/CmacAes128.cpp"
    "${SRC_DIR}/Poly1305.cpp"
    "${SRC_DIR}/Aes256Cbc.cpp"
    "${SRC_DIR}/Aes256Ctr.cpp"
    "${SRC_DIR}/Aes256Gcm.cpp"
    "${SRC_DIR}/Aes128Gcm.cpp"
    "${SRC_DIR}/Aes128Ccm.cpp"
    "${SRC_DIR}/ChaCha20Poly1305.cpp"
    "${SRC_DIR}/Hkdf.cpp"
    "${SRC_DIR}/SecureRandom.cpp"
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(OpenSSL QUIET)
if (NOT OpenSSL_FOUND)
  message(
    STATUS "${LIB_NAME}: OpenSSL not found -- skipping (cross-compile target may lack libssl)"
  )
  return()
endif ()

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
  utilities_compatibility
  OpenSSL::SSL
  OpenSSL::Crypto
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst tst)
