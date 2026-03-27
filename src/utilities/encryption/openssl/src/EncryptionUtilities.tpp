/**
 * @file EncryptionUtilities.tpp
 * @brief Template implementation for encryption utility functions.
 */
#ifndef APEX_ENCRYPTION_UTILITIES_TPP
#define APEX_ENCRYPTION_UTILITIES_TPP

#include "src/utilities/encryption/openssl/inc/EncryptionUtilities.hpp"

namespace apex::encryption {

template <typename Container> inline std::string toHex(const Container& c) {
  return toHex(std::span<const uint8_t>(c.data(), c.size()));
}

template <typename Container>
inline bool fromHex(const std::string& hexStr, Container& out) noexcept {
  std::vector<uint8_t> tmp;
  if (!fromHex(hexStr, tmp))
    return false;
  out.assign(tmp.begin(), tmp.end());
  return true;
}

} // namespace apex::encryption
#endif // ENCRYPTION_UTILITIES_TPP
