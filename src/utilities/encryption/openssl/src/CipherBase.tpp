/**
 * @file CipherBase.tpp
 * @brief Template implementation for symmetric cipher operations.
 */
#ifndef APEX_ENCRYPTION_CIPHER_BASE_TPP
#define APEX_ENCRYPTION_CIPHER_BASE_TPP

#include "src/utilities/encryption/openssl/inc/CipherBase.hpp"

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_openssl.hpp"

#include <algorithm>
#include <openssl/params.h> // OSSL_PARAM_*, EVP_CIPHER_CTX_set_params (3.x)

namespace apex::encryption {

// OpenSSL helper alias
namespace o = compat::ossl;

// Vector encrypt: size to worst-case (msg + BLOCK_SIZE), then shrink.
template <typename Derived>
inline typename CipherBase<Derived>::Status
CipherBase<Derived>::encrypt(bytes_span message, std::vector<std::uint8_t>& out) noexcept {
  out.clear();
  out.resize(message.size() + Derived::BLOCK_SIZE);
  std::size_t len = out.size();

  const Status st = encrypt(message, out.data(), len);
  out.resize(len);
  return st;
}

// Vector decrypt: size to msg size (CBC/CTR upper bound), then shrink.
template <typename Derived>
inline typename CipherBase<Derived>::Status
CipherBase<Derived>::decrypt(bytes_span message, std::vector<std::uint8_t>& out) noexcept {
  out.clear();
  out.resize(message.size());
  std::size_t len = out.size();

  const Status st = decrypt(message, out.data(), len);
  out.resize(len);
  return st;
}

// Zero-allocation encrypt.
template <typename Derived>
inline typename CipherBase<Derived>::Status
CipherBase<Derived>::encrypt(bytes_span message, std::uint8_t* outBuf,
                             std::size_t& outLen) noexcept {
  if (COMPAT_UNLIKELY(key_.size() != Derived::KEY_LENGTH))
    return Status::ERROR_INVALID_KEY;
  if (COMPAT_UNLIKELY(iv_.size() != Derived::IV_LENGTH))
    return Status::ERROR_INVALID_IV;

  // Resolve cipher (provider-aware on 3.x, pointer on 1.1.1).
  auto ch = o::fetchCipher<Derived>();
  const EVP_CIPHER* cipher = ch.cipher;
  if (COMPAT_UNLIKELY(cipher == nullptr))
    return Status::ERROR_INIT;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (COMPAT_UNLIKELY(ctx == nullptr))
    return Status::ERROR_INIT;

  // Phase 1: set cipher (no key/iv yet)
  if (COMPAT_UNLIKELY(EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // Optional provider params (OpenSSL 3.x only)
#if COMPAT_OPENSSL_MAJOR >= 3
  if (const char* pname = Derived::fetchParamName()) {
    const char* pval = Derived::fetchParamValue();
    OSSL_PARAM ps[2];
    ps[0] = OSSL_PARAM_construct_utf8_string(const_cast<char*>(pname), const_cast<char*>(pval), 0);
    ps[1] = OSSL_PARAM_construct_end();
    (void)EVP_CIPHER_CTX_set_params(ctx, ps);
  }
#endif

  // Phase 2: apply key/iv
  if (COMPAT_UNLIKELY(EVP_EncryptInit_ex(ctx, nullptr, nullptr, key_.data(), iv_.data()) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  // Update + Final
  int outl = 0;
  if (COMPAT_UNLIKELY(EVP_EncryptUpdate(ctx, outBuf, &outl, message.data(),
                                        static_cast<int>(message.size())) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_UPDATE;
  }

  int tail = 0;
  if (COMPAT_UNLIKELY(EVP_EncryptFinal_ex(ctx, outBuf + outl, &tail) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_FINAL;
  }

  EVP_CIPHER_CTX_free(ctx);
  outLen = static_cast<std::size_t>(outl + tail);
  return Status::SUCCESS;
}

// Zero-allocation decrypt.
template <typename Derived>
inline typename CipherBase<Derived>::Status
CipherBase<Derived>::decrypt(bytes_span message, std::uint8_t* outBuf,
                             std::size_t& outLen) noexcept {
  if (COMPAT_UNLIKELY(key_.size() != Derived::KEY_LENGTH))
    return Status::ERROR_INVALID_KEY;
  if (COMPAT_UNLIKELY(iv_.size() != Derived::IV_LENGTH))
    return Status::ERROR_INVALID_IV;

  auto ch = o::fetchCipher<Derived>();
  const EVP_CIPHER* cipher = ch.cipher;
  if (COMPAT_UNLIKELY(cipher == nullptr))
    return Status::ERROR_INIT;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (COMPAT_UNLIKELY(ctx == nullptr))
    return Status::ERROR_INIT;

  if (COMPAT_UNLIKELY(EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

#if COMPAT_OPENSSL_MAJOR >= 3
  if (const char* pname = Derived::fetchParamName()) {
    const char* pval = Derived::fetchParamValue();
    OSSL_PARAM ps[2];
    ps[0] = OSSL_PARAM_construct_utf8_string(const_cast<char*>(pname), const_cast<char*>(pval), 0);
    ps[1] = OSSL_PARAM_construct_end();
    (void)EVP_CIPHER_CTX_set_params(ctx, ps);
  }
#endif

  if (COMPAT_UNLIKELY(EVP_DecryptInit_ex(ctx, nullptr, nullptr, key_.data(), iv_.data()) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_INIT;
  }

  int outl = 0;
  if (COMPAT_UNLIKELY(EVP_DecryptUpdate(ctx, outBuf, &outl, message.data(),
                                        static_cast<int>(message.size())) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_UPDATE;
  }

  int tail = 0;
  if (COMPAT_UNLIKELY(EVP_DecryptFinal_ex(ctx, outBuf + outl, &tail) <= 0)) {
    EVP_CIPHER_CTX_free(ctx);
    return Status::ERROR_FINAL;
  }

  EVP_CIPHER_CTX_free(ctx);
  outLen = static_cast<std::size_t>(outl + tail);
  return Status::SUCCESS;
}

} // namespace apex::encryption

#endif // ENCRYPTION_CIPHER_BASE_TPP
