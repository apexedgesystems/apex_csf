# ==============================================================================
# Performance Tests: utilities_encryption
#
# Throughput and latency benchmarks for hashes, MACs, ciphers, AEADs, and KDFs.
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(PERF_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Encryption_pTest.cpp")

# ------------------------------------------------------------------------------
# Performance Tests
# ------------------------------------------------------------------------------

apex_add_ptest(
  TARGET
  Encryption_PTEST
  SOURCES
  ${PERF_TEST_SOURCES}
  LINK
  utilities_encryption
  OpenSSL::SSL
  OpenSSL::Crypto
)
