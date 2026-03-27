# ==============================================================================
# Unit Tests: utilities_encryption
#
# Unit tests for cryptographic primitives (hashing, MAC, ciphers, AEAD).
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (19 files)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/EncryptionUtilities_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sha256Hash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sha512Hash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Md5Hash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Blake2sHash_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sha3Hash256_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HmacSha256_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/HmacSha512_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CmacAes128_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Poly1305_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Aes256Cbc_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Aes256Ctr_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Aes256Gcm_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Aes128Gcm_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Aes128Ccm_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ChaCha20Poly1305_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Hkdf_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SecureRandom_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Interfaces_uTest.cpp"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestUtilitiesEncryption
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  utilities_encryption
  OpenSSL::SSL
  OpenSSL::Crypto
  fmt::fmt
  LABELS
  utilities
  encryption
)
