/**
 * @file AeadBase.tpp
 * @brief Template implementation for AEAD (Authenticated Encryption with Associated Data).
 */
#ifndef APEX_ENCRYPTION_AEAD_BASE_TPP
#define APEX_ENCRYPTION_AEAD_BASE_TPP

#include "src/utilities/encryption/openssl/inc/AeadBase.hpp"

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_openssl.hpp"

#include <algorithm>
#include <openssl/evp.h>
#include <openssl/params.h> // OSSL_PARAM (when used)

namespace apex::encryption {

// OpenSSL compat alias (provider fetch, version gating)
namespace o = compat::ossl;

// ------------------------------
// Vector-based one-shot APIs
// ------------------------------
template <typename Derived>
inline typename AeadBase<Derived>::Status
AeadBase<Derived>::encrypt(bytes_span plaintext, bytes_span aad,
                           std::vector<std::uint8_t>& outCipher,
                           std::vector<std::uint8_t>& outTag) noexcept {
  outCipher.clear();
  outCipher.resize(plaintext.size());

  outTag.clear();
  outTag.resize(Derived::TAG_LENGTH);

  std::size_t ctLen = outCipher.size();
  std::size_t tgLen = outTag.size();

  auto st = encrypt(plaintext, aad, outCipher.data(), ctLen, outTag.data(), tgLen);

  if (st == Status::SUCCESS) {
    outCipher.resize(ctLen);
    outTag.resize(tgLen);
  }
  return st;
}

template <typename Derived>
inline typename AeadBase<Derived>::Status
AeadBase<Derived>::decrypt(bytes_span ciphertext, bytes_span aad,
                           std::vector<std::uint8_t>& outPlain, bytes_span tag) noexcept {
  outPlain.clear();
  outPlain.resize(ciphertext.size());

  std::size_t ptLen = outPlain.size();

  auto st = decrypt(ciphertext, aad, outPlain.data(), ptLen, tag.data(), tag.size());

  if (st == Status::SUCCESS) {
    outPlain.resize(ptLen);
  }
  return st;
}

// ------------------------------
// Zero-allocation buffer APIs
// ------------------------------
template <typename Derived>
inline typename AeadBase<Derived>::Status
AeadBase<Derived>::encrypt(bytes_span plaintext, bytes_span aad, std::uint8_t* outBuf,
                           std::size_t& outLen, std::uint8_t* tagBuf,
                           std::size_t& tagLen) noexcept {
  if (COMPAT_UNLIKELY(key_.size() != Derived::KEY_LENGTH))
    return Status::ERROR_INVALID_KEY;
  if (COMPAT_UNLIKELY(iv_.size() != Derived::IV_LENGTH))
    return Status::ERROR_INVALID_IV;

  // Capacity/presence checks:
  // - tagBuf must exist and have room for TAG_LENGTH
  // - outBuf may be nullptr only when plaintext is empty
  if (COMPAT_UNLIKELY(tagBuf == nullptr))
    return Status::ERROR_OUTPUT_TOO_SMALL;
  if (COMPAT_UNLIKELY(outBuf == nullptr && !plaintext.empty()))
    return Status::ERROR_OUTPUT_TOO_SMALL;

  if (COMPAT_UNLIKELY(outLen < plaintext.size())) {
    outLen = plaintext.size();
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }
  if (COMPAT_UNLIKELY(tagLen < Derived::TAG_LENGTH)) {
    tagLen = Derived::TAG_LENGTH;
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }

  // Resolve cipher (provider-aware on 3.x; pointer fallback otherwise)
  auto ch = o::fetchCipher<Derived>();
  if (COMPAT_UNLIKELY(ch.cipher == nullptr))
    return Status::ERROR_NULL_ALGORITHM;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (COMPAT_UNLIKELY(ctx == nullptr))
    return Status::ERROR_INIT;

  // Initial setup (no key/iv yet)
  if (COMPAT_UNLIKELY(EVP_EncryptInit_ex(ctx, ch.cipher, nullptr, nullptr, nullptr) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // Optional provider parameters (3.x)
  if (const char* pname = Derived::fetchParamName()) {
#if COMPAT_OPENSSL_MAJOR >= 3
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(const_cast<char*>(pname),
                                                 const_cast<char*>(Derived::fetchParamValue()), 0);
    params[1] = OSSL_PARAM_construct_end();
    (void)EVP_CIPHER_CTX_set_params(ctx, params);
#endif
  }

  // AEAD IV length and key/iv
  if (COMPAT_UNLIKELY(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN,
                                          static_cast<int>(Derived::IV_LENGTH), nullptr) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }
  if (COMPAT_UNLIKELY(EVP_EncryptInit_ex(ctx, nullptr, nullptr, key_.data(), iv_.data()) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // AAD (may be empty)
  if (!aad.empty()) {
    int tmp = 0;
    if (COMPAT_UNLIKELY(
            EVP_EncryptUpdate(ctx, nullptr, &tmp, aad.data(), static_cast<int>(aad.size())) <= 0)) {
      EVP_CIPHER_CTX_free(ctx);
      return Status::ERROR_UPDATE;
    }
  }

  // Encrypt plaintext (skip when empty)
  int outl = 0;
  if (!plaintext.empty()) {
    if (COMPAT_UNLIKELY(EVP_EncryptUpdate(ctx, outBuf, &outl, plaintext.data(),
                                          static_cast<int>(plaintext.size())) <= 0)) {
      EVP_CIPHER_CTX_free(ctx);
      return Status::ERROR_UPDATE;
    }
  }

  // Final (GCM typically adds no extra bytes) — use scratch if outBuf==nullptr
  int tail = 0;
  std::uint8_t scratch[1];
  std::uint8_t* finalPtr = outBuf ? (outBuf + outl) : scratch;
  if (COMPAT_UNLIKELY(EVP_EncryptFinal_ex(ctx, finalPtr, &tail) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_FINAL;
  }

  // Get tag
  if (COMPAT_UNLIKELY(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG,
                                          static_cast<int>(Derived::TAG_LENGTH), tagBuf) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_FINAL;
  }

  EVP_CIPHER_CTX_free(ctx);
  outLen = static_cast<std::size_t>(outl + tail);
  tagLen = Derived::TAG_LENGTH;
  return Status::SUCCESS;
}

template <typename Derived>
inline typename AeadBase<Derived>::Status
AeadBase<Derived>::decrypt(bytes_span ciphertext, bytes_span aad, std::uint8_t* outBuf,
                           std::size_t& outLen, const std::uint8_t* tagBuf,
                           std::size_t tagLen) noexcept {
  if (COMPAT_UNLIKELY(key_.size() != Derived::KEY_LENGTH))
    return Status::ERROR_INVALID_KEY;
  if (COMPAT_UNLIKELY(iv_.size() != Derived::IV_LENGTH))
    return Status::ERROR_INVALID_IV;

  // Capacity/presence checks:
  // - tagBuf must exist and have exact TAG_LENGTH
  // - outBuf may be nullptr only when ciphertext is empty
  if (COMPAT_UNLIKELY(tagBuf == nullptr))
    return Status::ERROR_OUTPUT_TOO_SMALL;
  if (COMPAT_UNLIKELY(outBuf == nullptr && !ciphertext.empty()))
    return Status::ERROR_OUTPUT_TOO_SMALL;

  if (COMPAT_UNLIKELY(outLen < ciphertext.size())) {
    outLen = ciphertext.size();
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }

  // Tag length must match exactly for AEAD verify
  if (COMPAT_UNLIKELY(tagLen != Derived::TAG_LENGTH))
    return Status::ERROR_AUTH;

  auto ch = o::fetchCipher<Derived>();
  if (COMPAT_UNLIKELY(ch.cipher == nullptr))
    return Status::ERROR_NULL_ALGORITHM;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (COMPAT_UNLIKELY(ctx == nullptr))
    return Status::ERROR_INIT;

  if (COMPAT_UNLIKELY(EVP_DecryptInit_ex(ctx, ch.cipher, nullptr, nullptr, nullptr) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // Optional provider parameters (3.x)
  if (const char* pname = Derived::fetchParamName()) {
#if COMPAT_OPENSSL_MAJOR >= 3
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(const_cast<char*>(pname),
                                                 const_cast<char*>(Derived::fetchParamValue()), 0);
    params[1] = OSSL_PARAM_construct_end();
    (void)EVP_CIPHER_CTX_set_params(ctx, params);
#endif
  }

  if (COMPAT_UNLIKELY(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN,
                                          static_cast<int>(Derived::IV_LENGTH), nullptr) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }
  if (COMPAT_UNLIKELY(EVP_DecryptInit_ex(ctx, nullptr, nullptr, key_.data(), iv_.data()) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // AAD first
  if (!aad.empty()) {
    int tmp = 0;
    if (COMPAT_UNLIKELY(
            EVP_DecryptUpdate(ctx, nullptr, &tmp, aad.data(), static_cast<int>(aad.size())) <= 0)) {
      EVP_CIPHER_CTX_free(ctx);
      return Status::ERROR_UPDATE;
    }
  }

  // Provide expected tag before final
  if (COMPAT_UNLIKELY(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG,
                                          static_cast<int>(Derived::TAG_LENGTH),
                                          const_cast<std::uint8_t*>(tagBuf)) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // Decrypt ciphertext (skip when empty)
  int outl = 0;
  if (!ciphertext.empty()) {
    if (COMPAT_UNLIKELY(EVP_DecryptUpdate(ctx, outBuf, &outl, ciphertext.data(),
                                          static_cast<int>(ciphertext.size())) <= 0)) {
      EVP_CIPHER_CTX_free(ctx);
      return Status::ERROR_UPDATE;
    }
  }

  // Finalize + authenticate — use scratch if outBuf==nullptr
  int tail = 0;
  std::uint8_t scratch[1];
  std::uint8_t* finalPtr = outBuf ? (outBuf + outl) : scratch;
  if (COMPAT_UNLIKELY(EVP_DecryptFinal_ex(ctx, finalPtr, &tail) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_AUTH;
  }

  EVP_CIPHER_CTX_free(ctx);
  outLen = static_cast<std::size_t>(outl + tail);
  return Status::SUCCESS;
}

} // namespace apex::encryption

#endif // ENCRYPTION_AEAD_BASE_TPP
