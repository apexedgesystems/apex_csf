/**
 * @file Encryption_pTest.cpp
 * @brief Performance tests for OpenSSL-backed encryption primitives.
 *
 * Measures:
 *  - Hash throughput (SHA-256, SHA-512, MD5, BLAKE2s, SHA3-256)
 *  - SHA-256 payload scaling (64B to 1MB) and vector vs buffer API
 *  - MAC throughput (HMAC-SHA256/512, CMAC-AES128, Poly1305)
 *  - Cipher throughput (AES-256-CBC vs AES-256-CTR)
 *  - AEAD throughput (AES-256-GCM, AES-128-GCM, AES-128-CCM, ChaCha20-Poly1305)
 *  - KDF throughput (HKDF-SHA256, HKDF-SHA512)
 *  - SHA-256 large-payload bandwidth and AES-NI detection
 *
 * Usage:
 *   ./Encryption_PTEST --csv results.csv
 *   ./Encryption_PTEST --quick
 *   ./Encryption_PTEST --profile perf --gtest_filter="*Sha256*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/encryption/openssl/inc/Aes128Ccm.hpp"
#include "src/utilities/encryption/openssl/inc/Aes128Gcm.hpp"
#include "src/utilities/encryption/openssl/inc/Aes256Cbc.hpp"
#include "src/utilities/encryption/openssl/inc/Aes256Ctr.hpp"
#include "src/utilities/encryption/openssl/inc/Aes256Gcm.hpp"
#include "src/utilities/encryption/openssl/inc/Blake2sHash.hpp"
#include "src/utilities/encryption/openssl/inc/ChaCha20Poly1305.hpp"
#include "src/utilities/encryption/openssl/inc/CmacAes128.hpp"
#include "src/utilities/encryption/openssl/inc/Hkdf.hpp"
#include "src/utilities/encryption/openssl/inc/HmacSha256.hpp"
#include "src/utilities/encryption/openssl/inc/HmacSha512.hpp"
#include "src/utilities/encryption/openssl/inc/Md5Hash.hpp"
#include "src/utilities/encryption/openssl/inc/Poly1305.hpp"
#include "src/utilities/encryption/openssl/inc/Sha256Hash.hpp"
#include "src/utilities/encryption/openssl/inc/Sha3Hash256.hpp"
#include "src/utilities/encryption/openssl/inc/Sha512Hash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;
namespace enc = apex::encryption;

/* ----------------------------- Test Data ----------------------------- */

/**
 * @brief Generate random test data of specified size.
 */
static std::vector<uint8_t> generateTestData(size_t size) {
  std::vector<uint8_t> data(size);
  std::mt19937 gen(42); // Fixed seed for reproducibility
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  for (auto& byte : data) {
    byte = static_cast<uint8_t>(dist(gen));
  }
  return data;
}

/**
 * @brief Generate a fixed key of specified size.
 */
template <size_t N> static std::array<uint8_t, N> generateKey() {
  std::array<uint8_t, N> key{};
  for (size_t i = 0; i < N; ++i) {
    key[i] = static_cast<uint8_t>(i ^ 0xAB);
  }
  return key;
}

/**
 * @brief Generate a fixed IV of specified size.
 */
template <size_t N> static std::array<uint8_t, N> generateIv() {
  std::array<uint8_t, N> iv{};
  for (size_t i = 0; i < N; ++i) {
    iv[i] = static_cast<uint8_t>(i ^ 0xCD);
  }
  return iv;
}

/* ----------------------------- Hash Algorithm Comparison ----------------------------- */

/**
 * @brief Compare all hash algorithm throughputs.
 *
 * Tests with 4KB payload (fits in L1 cache) to isolate algorithm differences.
 */
PERF_TEST(Hash, AlgorithmComparison) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096; // 4KB
  auto data = generateTestData(PAYLOAD_SIZE);
  std::vector<uint8_t> digest;
  digest.reserve(64); // Max digest size

  std::printf("\n=== Hash Algorithm Comparison (4KB payload) ===\n");
  std::printf("%-12s %12s %12s %12s\n", "Algorithm", "ops/s", "MB/s", "us/call");
  std::printf("%s\n", std::string(52, '-').c_str());

  // SHA-256
  {
    enc::Sha256Hash hasher;
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        digest.clear();
        hasher.hash(data, digest);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          digest.clear();
          hasher.hash(data, digest);
        },
        "sha256");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-12s %12.0f %12.1f %12.3f\n", "SHA-256", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // SHA-512
  {
    enc::Sha512Hash hasher;
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        digest.clear();
        hasher.hash(data, digest);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          digest.clear();
          hasher.hash(data, digest);
        },
        "sha512");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-12s %12.0f %12.1f %12.3f\n", "SHA-512", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // MD5
  {
    enc::Md5Hash hasher;
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        digest.clear();
        hasher.hash(data, digest);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          digest.clear();
          hasher.hash(data, digest);
        },
        "md5");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-12s %12.0f %12.1f %12.3f\n", "MD5", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // BLAKE2s-256
  {
    enc::Blake2sHash hasher;
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        digest.clear();
        hasher.hash(data, digest);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          digest.clear();
          hasher.hash(data, digest);
        },
        "blake2s");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-12s %12.0f %12.1f %12.3f\n", "BLAKE2s", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // SHA3-256
  {
    enc::Sha3Hash256 hasher;
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        digest.clear();
        hasher.hash(data, digest);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          digest.clear();
          hasher.hash(data, digest);
        },
        "sha3-256");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-12s %12.0f %12.1f %12.3f\n", "SHA3-256", result.callsPerSecond, mbps,
                result.stats.median);
  }
}

/* ----------------------------- Hash Payload Scaling ----------------------------- */

/**
 * @brief Test SHA-256 throughput across different payload sizes.
 *
 * Reveals cache hierarchy effects and per-call overhead.
 */
PERF_TEST(Sha256, PayloadScaling) {
  UB_PERF_GUARD(perf);

  struct TestCase {
    const char* name;
    size_t size;
  };

  std::vector<TestCase> testCases = {{"64B", 64},       {"256B", 256},   {"1KB", 1024},
                                     {"4KB", 4096},     {"16KB", 16384}, {"64KB", 65536},
                                     {"256KB", 262144}, {"1MB", 1048576}};

  std::printf("\n=== SHA-256 Payload Scaling ===\n");
  std::printf("%-8s %12s %12s %12s\n", "Size", "ops/s", "MB/s", "ns/byte");
  std::printf("%s\n", std::string(48, '-').c_str());

  enc::Sha256Hash hasher;
  std::vector<uint8_t> digest;
  digest.reserve(32);

  for (const auto& test : testCases) {
    auto data = generateTestData(test.size);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        digest.clear();
        hasher.hash(data, digest);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          digest.clear();
          hasher.hash(data, digest);
        },
        test.name);

    double mbps = (test.size * result.callsPerSecond) / (1024.0 * 1024.0);
    double nsPerByte = (result.stats.median * 1000.0) / test.size;

    std::printf("%-8s %12.0f %12.1f %12.2f\n", test.name, result.callsPerSecond, mbps, nsPerByte);
  }
}

/* ----------------------------- Vector vs Buffer API ----------------------------- */

/**
 * @brief Compare vector API vs zero-allocation buffer API.
 *
 * Tests if avoiding vector operations provides measurable benefit.
 */
PERF_TEST(Sha256, VectorVsBuffer) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto data = generateTestData(PAYLOAD_SIZE);

  std::printf("\n=== SHA-256 Vector vs Buffer API (4KB) ===\n");

  enc::Sha256Hash hasher;

  // Vector API
  std::vector<uint8_t> vecDigest;
  vecDigest.reserve(32);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      vecDigest.clear();
      hasher.hash(data, vecDigest);
    }
  });

  auto vecResult = perf.throughputLoop(
      [&] {
        vecDigest.clear();
        hasher.hash(data, vecDigest);
      },
      "vector");

  std::printf("Vector API: %.0f ops/s (%.3f us/call)\n", vecResult.callsPerSecond,
              vecResult.stats.median);

  // Buffer API (zero-allocation)
  std::array<uint8_t, 32> bufDigest{};
  size_t bufLen = 32;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      bufLen = 32;
      hasher.hash(data, bufDigest.data(), bufLen);
    }
  });

  auto bufResult = perf.throughputLoop(
      [&] {
        bufLen = 32;
        hasher.hash(data, bufDigest.data(), bufLen);
      },
      "buffer");

  std::printf("Buffer API: %.0f ops/s (%.3f us/call)\n", bufResult.callsPerSecond,
              bufResult.stats.median);

  double speedup = bufResult.callsPerSecond / vecResult.callsPerSecond;
  std::printf("Buffer speedup: %.2fx\n", speedup);
}

/* ----------------------------- MAC Algorithm Comparison ----------------------------- */

/**
 * @brief Compare all MAC algorithm throughputs.
 */
PERF_TEST(Mac, AlgorithmComparison) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto data = generateTestData(PAYLOAD_SIZE);
  std::vector<uint8_t> tag;
  tag.reserve(64);

  std::printf("\n=== MAC Algorithm Comparison (4KB payload) ===\n");
  std::printf("%-14s %12s %12s %12s\n", "Algorithm", "ops/s", "MB/s", "us/call");
  std::printf("%s\n", std::string(54, '-').c_str());

  // HMAC-SHA256
  {
    enc::HmacSha256 mac;
    auto key = generateKey<32>();
    mac.setKey(key);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        tag.clear();
        mac.mac(data, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          tag.clear();
          mac.mac(data, tag);
        },
        "hmac-sha256");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-14s %12.0f %12.1f %12.3f\n", "HMAC-SHA256", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // HMAC-SHA512
  {
    enc::HmacSha512 mac;
    auto key = generateKey<64>();
    mac.setKey(key);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        tag.clear();
        mac.mac(data, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          tag.clear();
          mac.mac(data, tag);
        },
        "hmac-sha512");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-14s %12.0f %12.1f %12.3f\n", "HMAC-SHA512", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // CMAC-AES128
  {
    enc::CmacAes128 mac;
    auto key = generateKey<16>();
    mac.setKey(key);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        tag.clear();
        mac.mac(data, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          tag.clear();
          mac.mac(data, tag);
        },
        "cmac-aes128");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-14s %12.0f %12.1f %12.3f\n", "CMAC-AES128", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // Poly1305
  {
    enc::Poly1305 mac;
    auto key = generateKey<32>();
    mac.setKey(key);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        tag.clear();
        mac.mac(data, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          tag.clear();
          mac.mac(data, tag);
        },
        "poly1305");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-14s %12.0f %12.1f %12.3f\n", "Poly1305", result.callsPerSecond, mbps,
                result.stats.median);
  }
}

/* ----------------------------- Cipher Comparison ----------------------------- */

/**
 * @brief Compare AES-256-CBC vs AES-256-CTR throughput.
 */
PERF_TEST(Cipher, ModeComparison) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto plaintext = generateTestData(PAYLOAD_SIZE);
  auto key = generateKey<32>();
  auto iv = generateIv<16>();

  std::vector<uint8_t> ciphertext;
  ciphertext.reserve(PAYLOAD_SIZE + 16); // CBC may pad

  std::printf("\n=== Cipher Mode Comparison (4KB payload) ===\n");
  std::printf("%-14s %12s %12s %12s\n", "Mode", "ops/s", "MB/s", "us/call");
  std::printf("%s\n", std::string(54, '-').c_str());

  // AES-256-CBC Encrypt
  {
    enc::Aes256Cbc cipher;
    cipher.setKey(key);
    cipher.setIv(iv);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        ciphertext.clear();
        cipher.encrypt(plaintext, ciphertext);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          ciphertext.clear();
          cipher.encrypt(plaintext, ciphertext);
        },
        "aes256-cbc-enc");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-14s %12.0f %12.1f %12.3f\n", "AES-256-CBC", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // AES-256-CTR Encrypt
  {
    enc::Aes256Ctr cipher;
    cipher.setKey(key);
    cipher.setIv(iv);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        ciphertext.clear();
        cipher.encrypt(plaintext, ciphertext);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          ciphertext.clear();
          cipher.encrypt(plaintext, ciphertext);
        },
        "aes256-ctr-enc");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-14s %12.0f %12.1f %12.3f\n", "AES-256-CTR", result.callsPerSecond, mbps,
                result.stats.median);
  }
}

/* ----------------------------- AEAD Benchmark ----------------------------- */

/**
 * @brief AES-256-GCM encrypt/decrypt throughput.
 */
PERF_TEST(Aead, Aes256GcmThroughput) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto plaintext = generateTestData(PAYLOAD_SIZE);
  auto aad = generateTestData(16); // Associated data
  auto key = generateKey<32>();
  auto iv = generateIv<12>(); // GCM uses 12-byte IV

  std::vector<uint8_t> ciphertext, tag, decrypted;
  ciphertext.reserve(PAYLOAD_SIZE);
  tag.reserve(16);
  decrypted.reserve(PAYLOAD_SIZE);

  std::printf("\n=== AES-256-GCM Throughput (4KB payload + 16B AAD) ===\n");

  enc::Aes256Gcm aead;
  aead.setKey(key);
  aead.setIv(iv);

  // Encrypt
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      ciphertext.clear();
      tag.clear();
      aead.encrypt(plaintext, aad, ciphertext, tag);
    }
  });

  auto encResult = perf.throughputLoop(
      [&] {
        ciphertext.clear();
        tag.clear();
        aead.encrypt(plaintext, aad, ciphertext, tag);
      },
      "gcm-encrypt");

  double encMbps = (PAYLOAD_SIZE * encResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Encrypt: %.0f ops/s  %.1f MB/s  (%.3f us/call)\n", encResult.callsPerSecond, encMbps,
              encResult.stats.median);

  // Decrypt (using ciphertext from encrypt)
  aead.encrypt(plaintext, aad, ciphertext, tag); // Get valid ciphertext+tag

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      decrypted.clear();
      aead.decrypt(ciphertext, aad, decrypted, tag);
    }
  });

  auto decResult = perf.throughputLoop(
      [&] {
        decrypted.clear();
        aead.decrypt(ciphertext, aad, decrypted, tag);
      },
      "gcm-decrypt");

  double decMbps = (PAYLOAD_SIZE * decResult.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("Decrypt: %.0f ops/s  %.1f MB/s  (%.3f us/call)\n", decResult.callsPerSecond, decMbps,
              decResult.stats.median);

  double ratio = decResult.callsPerSecond / encResult.callsPerSecond;
  std::printf("Decrypt/Encrypt ratio: %.2f\n", ratio);
}

/* ----------------------------- AEAD Algorithm Comparison ----------------------------- */

/**
 * @brief Compare all AEAD algorithm throughputs.
 *
 * Critical for choosing the right AEAD:
 *  - AES-256-GCM: Best with AES-NI
 *  - AES-128-GCM: Slightly faster key schedule
 *  - ChaCha20-Poly1305: Best on ARM without AES-NI
 *  - AES-128-CCM: Required for BLE/Zigbee/Matter
 */
PERF_TEST(Aead, AlgorithmComparison) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto plaintext = generateTestData(PAYLOAD_SIZE);
  auto aad = generateTestData(16);

  std::printf("\n=== AEAD Algorithm Comparison (4KB payload + 16B AAD) ===\n");
  std::printf("%-20s %12s %12s %12s\n", "Algorithm", "ops/s", "MB/s", "us/call");
  std::printf("%s\n", std::string(60, '-').c_str());

  // AES-256-GCM
  {
    auto key = generateKey<32>();
    auto iv = generateIv<12>();
    std::vector<uint8_t> ct, tag;
    ct.reserve(PAYLOAD_SIZE);
    tag.reserve(16);

    enc::Aes256Gcm aead;
    aead.setKey(key);
    aead.setIv(iv);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        ct.clear();
        tag.clear();
        aead.encrypt(plaintext, aad, ct, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          ct.clear();
          tag.clear();
          aead.encrypt(plaintext, aad, ct, tag);
        },
        "aes256-gcm");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f %12.3f\n", "AES-256-GCM", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // AES-128-GCM
  {
    auto key = generateKey<16>();
    auto iv = generateIv<12>();
    std::vector<uint8_t> ct, tag;
    ct.reserve(PAYLOAD_SIZE);
    tag.reserve(16);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        ct.clear();
        tag.clear();
        enc::aes128GcmEncrypt(plaintext, aad, key, iv, ct, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          ct.clear();
          tag.clear();
          enc::aes128GcmEncrypt(plaintext, aad, key, iv, ct, tag);
        },
        "aes128-gcm");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f %12.3f\n", "AES-128-GCM", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // ChaCha20-Poly1305
  {
    auto key = generateKey<32>();
    auto nonce = generateIv<12>();
    std::vector<uint8_t> ct, tag;
    ct.reserve(PAYLOAD_SIZE);
    tag.reserve(16);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        ct.clear();
        tag.clear();
        enc::chacha20Poly1305Encrypt(plaintext, aad, key, nonce, ct, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          ct.clear();
          tag.clear();
          enc::chacha20Poly1305Encrypt(plaintext, aad, key, nonce, ct, tag);
        },
        "chacha20-poly1305");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f %12.3f\n", "ChaCha20-Poly1305", result.callsPerSecond, mbps,
                result.stats.median);
  }

  // AES-128-CCM (IoT)
  {
    auto key = generateKey<16>();
    std::array<uint8_t, 13> nonce{};
    for (size_t i = 0; i < 13; ++i)
      nonce[i] = static_cast<uint8_t>(i ^ 0xCD);
    std::vector<uint8_t> ct, tag;
    ct.reserve(PAYLOAD_SIZE);
    tag.reserve(16);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        ct.clear();
        tag.clear();
        enc::aes128CcmEncrypt(plaintext, aad, key, nonce, 16, ct, tag);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          ct.clear();
          tag.clear();
          enc::aes128CcmEncrypt(plaintext, aad, key, nonce, 16, ct, tag);
        },
        "aes128-ccm");

    double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);
    std::printf("%-20s %12.0f %12.1f %12.3f\n", "AES-128-CCM", result.callsPerSecond, mbps,
                result.stats.median);
  }
}

/* ----------------------------- HKDF Benchmark ----------------------------- */

/**
 * @brief HKDF key derivation throughput.
 *
 * Measures extract+expand performance for deriving keys from shared secrets.
 */
PERF_TEST(Kdf, HkdfThroughput) {
  UB_PERF_GUARD(perf);

  // Simulate 32-byte shared secret (typical ECDH output)
  auto ikm = generateTestData(32);
  auto salt = generateTestData(32);
  auto info = generateTestData(16);
  std::vector<uint8_t> derivedKey;
  derivedKey.reserve(64);

  std::printf("\n=== HKDF Key Derivation Throughput ===\n");
  std::printf("%-14s %12s %12s\n", "Algorithm", "ops/s", "us/call");
  std::printf("%s\n", std::string(40, '-').c_str());

  // HKDF-SHA256 (32-byte output)
  {
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        derivedKey.clear();
        enc::hkdfSha256(ikm, salt, info, 32, derivedKey);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          derivedKey.clear();
          enc::hkdfSha256(ikm, salt, info, 32, derivedKey);
        },
        "hkdf-sha256-32");

    std::printf("%-14s %12.0f %12.3f\n", "HKDF-SHA256", result.callsPerSecond, result.stats.median);
  }

  // HKDF-SHA512 (64-byte output)
  {
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        derivedKey.clear();
        enc::hkdfSha512(ikm, salt, info, 64, derivedKey);
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          derivedKey.clear();
          enc::hkdfSha512(ikm, salt, info, 64, derivedKey);
        },
        "hkdf-sha512-64");

    std::printf("%-14s %12.0f %12.3f\n", "HKDF-SHA512", result.callsPerSecond, result.stats.median);
  }
}

/* ----------------------------- Large Payload Throughput ----------------------------- */

/**
 * @brief Measure maximum throughput with large payloads.
 *
 * Tests memory bandwidth limits with 16MB payload.
 */
PERF_TEST(Sha256, LargePayload) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 16 * 1024 * 1024; // 16MB
  auto data = generateTestData(PAYLOAD_SIZE);

  enc::Sha256Hash hasher;
  std::array<uint8_t, 32> digest{};
  size_t digestLen = 32;

  // Single warmup for large payload
  hasher.hash(data, digest.data(), digestLen);

  // Manual timing with fewer iterations
  const int REPS = 5;
  std::vector<double> latencies;
  latencies.reserve(REPS);

  for (int r = 0; r < REPS; ++r) {
    auto start = std::chrono::steady_clock::now();
    digestLen = 32;
    hasher.hash(data, digest.data(), digestLen);
    auto end = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count();
    latencies.push_back(us);
  }

  std::sort(latencies.begin(), latencies.end());
  double median = latencies[latencies.size() / 2];
  double callsPerSec = 1'000'000.0 / median;
  double mbps = (PAYLOAD_SIZE * callsPerSec) / (1024.0 * 1024.0);
  double gbps = mbps / 1024.0;

  std::printf("\n=== SHA-256 Large Payload Throughput (16MB) ===\n");
  std::printf("Throughput: %.1f MB/s (%.2f GB/s)\n", mbps, gbps);
  std::printf("Latency: %.2f ms per 16MB\n", median / 1000.0);

  // Expect reasonable throughput (>100 MB/s minimum)
}

/* ----------------------------- AES-NI Detection ----------------------------- */

/**
 * @brief Detect hardware acceleration availability.
 *
 * OpenSSL automatically uses AES-NI when available.
 */
PERF_TEST(Aead, HardwareAcceleration) {
  UB_PERF_GUARD(perf);

  const size_t PAYLOAD_SIZE = 4096;
  auto plaintext = generateTestData(PAYLOAD_SIZE);
  auto aad = generateTestData(16);
  auto key = generateKey<32>();
  auto iv = generateIv<12>();

  std::vector<uint8_t> ciphertext, tag;
  ciphertext.reserve(PAYLOAD_SIZE);
  tag.reserve(16);

  enc::Aes256Gcm aead;
  aead.setKey(key);
  aead.setIv(iv);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      ciphertext.clear();
      tag.clear();
      aead.encrypt(plaintext, aad, ciphertext, tag);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        ciphertext.clear();
        tag.clear();
        aead.encrypt(plaintext, aad, ciphertext, tag);
      },
      "gcm-hw-check");

  double mbps = (PAYLOAD_SIZE * result.callsPerSecond) / (1024.0 * 1024.0);

  std::printf("\n=== Hardware Acceleration Check ===\n");
  std::printf("AES-256-GCM: %.1f MB/s\n", mbps);

  // AES-NI typically achieves >500 MB/s on 4KB payloads
  // Software-only is typically <100 MB/s
  if (mbps > 300.0) {
    std::printf("Status: Likely using AES-NI hardware acceleration\n");
  } else if (mbps > 100.0) {
    std::printf("Status: Moderate performance (may have partial HW support)\n");
  } else {
    std::printf("Status: Likely software-only (no AES-NI detected)\n");
  }

  // Expect at least 100 MB/s on any modern CPU
}

PERF_MAIN()
