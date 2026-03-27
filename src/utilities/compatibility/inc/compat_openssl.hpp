#ifndef APEX_UTILITIES_COMPATIBILITY_OPENSSL_HPP
#define APEX_UTILITIES_COMPATIBILITY_OPENSSL_HPP
/**
 * @file compat_openssl.hpp
 * @brief Version-safe helpers for OpenSSL 1.1.1 and 3.x (providers, fetch, RAII).
 *
 * Library-agnostic utilities:
 *  - Load OpenSSL 3.x providers (no-op on 1.1.1).
 *  - Fetch algorithms by provider name on 3.x, or fall back to pointer APIs.
 *  - Manage fetched descriptors with RAII (free on 3.x; no-op on 1.1.1).
 */

#include <mutex>
#include <type_traits>

// OpenSSL headers
#include <openssl/evp.h>
#include <openssl/opensslv.h>

#if defined(OPENSSL_VERSION_MAJOR)
#define COMPAT_OPENSSL_MAJOR OPENSSL_VERSION_MAJOR
#else
// OPENSSL_VERSION_MAJOR exists in 3.x; assume 1.x otherwise.
#define COMPAT_OPENSSL_MAJOR 1
#endif

#if COMPAT_OPENSSL_MAJOR >= 3
#include <openssl/provider.h>
#ifndef COMPAT_OPENSSL_AUTOLOAD_PROVIDERS
#define COMPAT_OPENSSL_AUTOLOAD_PROVIDERS 1
#endif
#ifndef COMPAT_OPENSSL_LOAD_LEGACY
#define COMPAT_OPENSSL_LOAD_LEGACY 0
#endif
#endif

namespace apex::compat::ossl {

/* ----------------------------- Initialization ----------------------------- */
/**
 * @brief Ensure OpenSSL 3.x providers are loaded (no-op on 1.1.1).
 *
 * Loads the "default" provider, and optionally "legacy" when
 * COMPAT_OPENSSL_LOAD_LEGACY is non-zero.
 */
inline void ensureProvidersLoaded() {
#if COMPAT_OPENSSL_MAJOR >= 3
#if COMPAT_OPENSSL_AUTOLOAD_PROVIDERS
  static std::once_flag once;
  std::call_once(once, [] {
    (void)OSSL_PROVIDER_load(nullptr, "default");
#if COMPAT_OPENSSL_LOAD_LEGACY
    (void)OSSL_PROVIDER_load(nullptr, "legacy");
#endif
  });
#endif
#endif
}

/* --------------------------- Type Traits (SFINAE) ------------------------- */
/**
 * @brief Detector idiom for provider-name fetchers on Derived types.
 *
 * Traits are true only when the member exists *and* returns `const char*`.
 */
template <class, class = void> struct HasMdName : std::false_type {};

template <class T>
struct HasMdName<T, std::void_t<decltype(T::fetchHashName())>>
    : std::bool_constant<std::is_same_v<decltype(T::fetchHashName()), const char*> ||
                         std::is_same_v<decltype(T::fetchHashName()), const char* const>> {};

template <class, class = void> struct HasCipherName : std::false_type {};

template <class T>
struct HasCipherName<T, std::void_t<decltype(T::fetchCipherName())>>
    : std::bool_constant<std::is_same_v<decltype(T::fetchCipherName()), const char*> ||
                         std::is_same_v<decltype(T::fetchCipherName()), const char* const>> {};

template <class, class = void> struct HasMacName : std::false_type {};

template <class T>
struct HasMacName<T, std::void_t<decltype(T::fetchMacName())>>
    : std::bool_constant<std::is_same_v<decltype(T::fetchMacName()), const char*> ||
                         std::is_same_v<decltype(T::fetchMacName()), const char* const>> {};

/** @brief Public constexpr flags for easy use in `if constexpr`. */
template <class T> inline constexpr bool HAS_MD_NAME_V = HasMdName<T>::value;
template <class T> inline constexpr bool HAS_CIPHER_NAME_V = HasCipherName<T>::value;
template <class T> inline constexpr bool HAS_MAC_NAME_V = HasMacName<T>::value;

/* --------------------------------- Types ---------------------------------- */
/**
 * @brief RAII holder for fetched EVP_MD descriptors.
 *
 * On OpenSSL 3.x, fetched objects are owned and freed.
 * On OpenSSL 1.1.1, pointers are library-owned and not freed.
 */
struct MdHandle {
  const EVP_MD* md = nullptr;
  bool must_free = false;
  ~MdHandle() {
#if COMPAT_OPENSSL_MAJOR >= 3
    if (must_free && md)
      EVP_MD_free(const_cast<EVP_MD*>(md));
#endif
  }
};

struct CipherHandle {
  const EVP_CIPHER* cipher = nullptr;
  bool must_free = false;
  ~CipherHandle() {
#if COMPAT_OPENSSL_MAJOR >= 3
    if (must_free && cipher)
      EVP_CIPHER_free(const_cast<EVP_CIPHER*>(cipher));
#endif
  }
};

// OpenSSL 3.x has EVP_MAC; 1.1.1 uses HMAC_* / CMAC_* APIs (no EVP_MAC).
#if COMPAT_OPENSSL_MAJOR >= 3
#include <openssl/evp.h>
struct MacHandle {
  EVP_MAC* mac = nullptr;
  bool must_free = false;
  ~MacHandle() {
    if (must_free && mac)
      EVP_MAC_free(mac);
  }
};
#else
struct MacHandle {
  void* mac = nullptr;
  bool must_free = false;
  ~MacHandle() = default;
};
#endif

/* ---------------------------------- API ----------------------------------- */
/**
 * @brief Fetch an EVP_MD* (prefer provider name on 3.x, fallback to pointer).
 * @tparam Derived Type providing fetchHashName() and/or fetchHashAlgorithm().
 */
template <class Derived> inline MdHandle fetchMd() {
  MdHandle h{};
#if COMPAT_OPENSSL_MAJOR >= 3
  ensureProvidersLoaded();
  if constexpr (HAS_MD_NAME_V<Derived>) {
    if (auto name = Derived::fetchHashName(); name && *name) {
      h.md = EVP_MD_fetch(nullptr, name, nullptr);
      h.must_free = (h.md != nullptr);
      if (h.md)
        return h;
    }
  }
#endif
  h.md = Derived::fetchHashAlgorithm(); // pointer path works everywhere
  return h;
}

/**
 * @brief Fetch an EVP_CIPHER* (prefer provider name on 3.x, fallback to pointer).
 * @tparam Derived Type providing fetchCipherName() and/or fetchCipherAlgorithm().
 */
template <class Derived> inline CipherHandle fetchCipher() {
  CipherHandle h{};
#if COMPAT_OPENSSL_MAJOR >= 3
  ensureProvidersLoaded();
  if constexpr (HAS_CIPHER_NAME_V<Derived>) {
    if (auto name = Derived::fetchCipherName(); name && *name) {
      h.cipher = EVP_CIPHER_fetch(nullptr, name, nullptr);
      h.must_free = (h.cipher != nullptr);
      if (h.cipher)
        return h;
    }
  }
#endif
  h.cipher = Derived::fetchCipherAlgorithm(); // pointer path
  return h;
}

/**
 * @brief Fetch an EVP_MAC* on 3.x; 1.1.1 signals fallback (HMAC/CMAC APIs).
 * @tparam Derived Type providing fetchMacName() when applicable.
 */
#if COMPAT_OPENSSL_MAJOR >= 3
#include <openssl/core_names.h>
#include <openssl/evp.h>
template <class Derived> inline MacHandle fetchMac() {
  MacHandle h{};
  ensureProvidersLoaded();
  if constexpr (HAS_MAC_NAME_V<Derived>) {
    if (auto name = Derived::fetchMacName(); name && *name) {
      h.mac = EVP_MAC_fetch(nullptr, name, nullptr);
      h.must_free = (h.mac != nullptr);
    }
  }
  return h; // may be null; caller decides fallback path
}
#else
template <class Derived> inline MacHandle fetchMac() {
  return {};
} // signal: use HMAC_* / CMAC_* path
#endif

} // namespace apex::compat::ossl

#endif // APEX_UTILITIES_COMPATIBILITY_OPENSSL_HPP
