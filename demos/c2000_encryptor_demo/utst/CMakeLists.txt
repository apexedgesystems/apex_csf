# ==============================================================================
# c2000_encryptor_demo - Unit Tests (host-run)
#
# Compiles the demo's C AES-256-GCM header on the host and locks it to the
# shared encryption_mcu implementation via the shared NIST vector table.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(UNIT_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Aes256Gcm_uTest.cpp")

# ------------------------------------------------------------------------------
# Test Target
# ------------------------------------------------------------------------------

apex_add_gtest(
  TARGET
  TestC2000EncryptorAes
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  encryption_mcu
  LABELS
  demos
)
