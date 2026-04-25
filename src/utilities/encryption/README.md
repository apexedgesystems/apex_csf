# Encryption

**Namespace:** `apex::encryption`
**Platform:** Linux
**C++ Standard:** C++17
**Dependencies:** OpenSSL (libssl, libcrypto)

Cryptographic primitives for real-time and embedded systems with zero-allocation APIs.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Module Reference](#3-module-reference)
4. [Dependencies](#4-dependencies)
5. [Testing](#5-testing)
6. [See Also](#6-see-also)

---

## 1. Quick Start

```cpp
#include "src/utilities/encryption/inc/Sha256Hash.hpp"
#include <vector>

std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
std::vector<uint8_t> digest;
digest.reserve(32);

auto status = apex::encryption::sha256Hash(data, digest);
if (status == apex::encryption::Sha256Hash::Status::SUCCESS) {
  // digest contains 32-byte SHA-256 hash
}
```

---

## 2. Key Features

- **Zero-allocation APIs** - Vector and caller-buffer variants for all operations
- **Uniform status codes** - Consistent `uint8_t` error handling across all modules
- **RT optimizations** - Inline, hot attributes, branch hints for predictable performance
- **CRTP extensibility** - `HashBase`, `MacBase`, `CipherBase`, `AeadBase` templates
- **OpenSSL 1.1.1/3.x compatible** - Supports both legacy and modern OpenSSL

---

## 3. Module Reference

### 3.1 Hashing

**RT-safe:** Yes (bounded OpenSSL calls, no heap allocation in buffer API)

| Algorithm   | Digest Size | Notes                              |
| ----------- | ----------- | ---------------------------------- |
| MD5         | 128 bits    | Legacy checksums; collision-broken |
| SHA-256     | 256 bits    | General-purpose, HMAC              |
| SHA-512     | 512 bits    | Extra collision margin             |
| BLAKE2s-256 | 256 bits    | High-speed, keyed MAC support      |
| SHA3-256    | 256 bits    | Sponge construction, FIPS-202      |

```cpp
// Vector API
std::vector<uint8_t> digest;
auto status = apex::encryption::sha256Hash(dataSpan, digest);

// Buffer API (zero-allocation)
uint8_t buffer[32];
size_t length = 32;
auto status = apex::encryption::sha256Hash(dataSpan, buffer, length);
```

### 3.2 Message Authentication Codes (MAC)

**RT-safe:** Yes (bounded OpenSSL calls, no heap allocation in buffer API)

| Algorithm   | Key Length | Tag Length | Notes                             |
| ----------- | ---------- | ---------- | --------------------------------- |
| HMAC-SHA256 | 32 bytes   | 32 bytes   | Hash-based MAC; general-purpose   |
| HMAC-SHA512 | 64 bytes   | 64 bytes   | Long tag for collision resistance |
| CMAC-AES128 | 16 bytes   | 16 bytes   | Block-cipher MAC; FIPS-approved   |
| Poly1305    | 32 bytes   | 16 bytes   | One-time, ultra-fast streaming    |

```cpp
apex::encryption::HmacSha256 mac;
mac.setKey(keySpan);
auto status = mac.mac(messageSpan, tagOut);
```

### 3.3 Symmetric Ciphers

**RT-safe:** Yes (bounded OpenSSL calls, no heap allocation in buffer API)

| Mode        | Key Length | IV Length | Notes                         |
| ----------- | ---------- | --------- | ----------------------------- |
| AES-256-CBC | 32 bytes   | 16 bytes  | Classic block-encryption mode |
| AES-256-CTR | 32 bytes   | 16 bytes  | Stream mode via counter       |

```cpp
std::vector<uint8_t> ciphertext;
auto status = apex::encryption::aes256CbcEncrypt(plaintext, key, iv, ciphertext);
```

### 3.4 AEAD Ciphers

**RT-safe:** Yes (bounded OpenSSL calls, no heap allocation in buffer API)

| Mode              | Key Length | IV/Nonce   | Tag Length | Notes               |
| ----------------- | ---------- | ---------- | ---------- | ------------------- |
| AES-256-GCM       | 32 bytes   | 12 bytes   | 16 bytes   | Galois/Counter Mode |
| AES-128-GCM       | 16 bytes   | 12 bytes   | 16 bytes   | Faster key schedule |
| AES-128-CCM       | 16 bytes   | 7-13 bytes | 4-16 bytes | BLE, Zigbee, Matter |
| ChaCha20-Poly1305 | 32 bytes   | 12 bytes   | 16 bytes   | ARM without AES-NI  |

```cpp
// AES-GCM (common usage)
std::vector<uint8_t> ciphertext, tag;
auto status = apex::encryption::aes256GcmEncrypt(
    plaintext, aad, key, iv, ciphertext, tag);

// ChaCha20-Poly1305 (ARM-optimized)
auto status = apex::encryption::chacha20Poly1305Encrypt(
    plaintext, aad, key, nonce, ciphertext, tag);

// AES-CCM (IoT protocols)
auto status = apex::encryption::aes128CcmEncrypt(
    plaintext, aad, key, nonce, tagLength, ciphertext, tag);
```

### 3.5 Key Derivation

**RT-safe:** Yes (bounded operations, no heap allocation after reserve)

| Algorithm | Hash Options     | Use Case                          |
| --------- | ---------------- | --------------------------------- |
| HKDF      | SHA-256, SHA-512 | TLS 1.3, Signal, WireGuard, Noise |

```cpp
// Derive key from shared secret (e.g., after ECDH)
std::vector<uint8_t> derivedKey;
auto status = apex::encryption::hkdfSha256(
    sharedSecret,   // IKM: Input Key Material
    salt,           // Optional salt
    "my-app-v1"_sp, // Context info
    32,             // Desired key length
    derivedKey);
```

### 3.6 Secure Random

**RT-safe:** No (may block on entropy exhaustion)

| API           | Notes                                  |
| ------------- | -------------------------------------- |
| fill()        | Fill buffer with random bytes          |
| generate()    | Fill vector/array with random bytes    |
| generateIv()  | Convenience wrapper for IV generation  |
| generateKey() | Convenience wrapper for key generation |

```cpp
// Generate IV for encryption
std::array<uint8_t, 12> iv;
auto status = apex::encryption::generateIv(iv);

// Generate random key
std::array<uint8_t, 32> key;
auto status = apex::encryption::generateKey(key);
```

### 3.7 MCU (Bare-Metal AES-256-GCM)

**Namespace:** `apex::encryption::mcu`
**Header:** `src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp`
**RT-safe:** No (variable-time S-box lookups; use hardware AES for side-channel resistance)

Standalone software AES-256-GCM with no OpenSSL or OS dependencies. Header-only,
zero heap allocation, suitable for Cortex-M4 and similar bare-metal targets.

| API                | Notes                                      |
| ------------------ | ------------------------------------------ |
| aes256GcmEncrypt() | Encrypt with authenticated additional data |
| aes256GcmDecrypt() | Decrypt with tag verification              |

```cpp
#include "src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp"

namespace mcu = apex::encryption::mcu;

std::array<uint8_t, 32> key{};
std::array<uint8_t, 12> nonce{};
std::array<uint8_t, 16> tag{};
std::vector<uint8_t> ciphertext(plaintext.size());

auto result = mcu::aes256GcmEncrypt(
    key.data(), nonce.data(),
    aad.data(), aad.size(),
    plaintext.data(), plaintext.size(),
    ciphertext.data(), tag.data());
```

**Performance (x86-64, 256B payload, 15 repeats):**

| Operation | Median (us) | CV%  | Throughput |
| --------- | ----------- | ---- | ---------- |
| Encrypt   | 182.1       | 1.1% | 1.34 MB/s  |
| Decrypt   | 176.7       | 0.6% | 1.38 MB/s  |

Bottleneck is GF(2^128) multiply for GHASH (73% of CPU time). This is a
bare-metal implementation optimized for code size, not throughput. For
high-throughput use cases, use the OpenSSL wrapper (Section 3.4) which
provides AES-NI + CLMUL hardware acceleration at ~3 GB/s.

---

## 4. Dependencies

| Dependency              | Version     | Required | Notes                          |
| ----------------------- | ----------- | -------- | ------------------------------ |
| OpenSSL                 | 1.1.1 / 3.x | Yes      | libssl, libcrypto              |
| utilities_compatibility | -           | Yes      | C++17/20 shims, OpenSSL compat |
| fmt                     | 8.0+        | Yes      | Formatting (tests only)        |
| GTest                   | 1.11+       | Tests    | Unit testing framework         |

---

## 5. Testing

```bash
# Build and run unit tests
docker compose run --rm -T dev-cuda make test

# Run encryption tests only
docker compose run --rm -T dev-cuda \
  ./build/native-linux-debug/bin/tests/TestUtilitiesEncryption
```

### Performance

Throughput on x86-64 with AES-NI (4KB payload, 15 repeats):

**Hash Algorithms:**

| Algorithm | ops/s | MB/s | us/call |   CV% |
| --------- | ----: | ---: | ------: | ----: |
| SHA-256   |  399K | 1560 |    2.51 |  2.2% |
| SHA-512   |  193K |  754 |    5.17 |  3.3% |
| MD5       |  216K |  844 |    4.62 |  2.6% |
| BLAKE2s   |  166K |  649 |    6.03 |  3.6% |
| SHA3-256  |  127K |  496 |    7.90 | 10.7% |

**AEAD Ciphers:**

| Algorithm           | ops/s | MB/s | us/call |  CV% |
| ------------------- | ----: | ---: | ------: | ---: |
| AES-256-GCM encrypt |  725K | 2832 |    1.38 | 4.5% |
| AES-256-GCM decrypt |  746K | 2914 |    1.34 | 3.9% |
| AES-128-GCM         |  721K | 2816 |    1.39 | 3.8% |
| ChaCha20-Poly1305   |  425K | 1660 |    2.35 | 8.3% |
| AES-128-CCM         |  397K | 1551 |    2.52 | 2.2% |

**Symmetric Ciphers:**

| Mode        | ops/s | MB/s | us/call |  CV% |
| ----------- | ----: | ---: | ------: | ---: |
| AES-256-CTR |  1.0M | 3906 |    0.99 | 3.0% |
| AES-256-CBC |  345K | 1348 |    2.90 | 3.4% |

All operations delegate to OpenSSL's EVP API with automatic AES-NI
hardware acceleration.

---

## 6. See Also

- **OpenSSL EVP API** - <https://www.openssl.org/docs/man3.0/man7/evp.html>
- **NIST FIPS-180-4** - Secure Hash Standard (SHA-2)
- **NIST FIPS-202** - SHA-3 Standard
- **RFC 2104** - HMAC
- **RFC 3610** - AES-CCM Mode
- **RFC 5869** - HKDF (HMAC-based Key Derivation Function)
- **RFC 7539** - ChaCha20/Poly1305
- **RFC 8439** - ChaCha20-Poly1305 for IETF Protocols
