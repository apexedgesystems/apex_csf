#ifndef APEX_SYSTEM_CORE_INFRASTRUCTURE_PROTOCOLS_APROTO_UTST_HEADER_WORDS_HPP
#define APEX_SYSTEM_CORE_INFRASTRUCTURE_PROTOCOLS_APROTO_UTST_HEADER_WORDS_HPP
/**
 * @file HeaderWords.hpp
 * @brief The multi-byte members of a packed AprotoHeader as plain values, for
 *        the tests' comparisons: a GTest assertion binds references to its
 *        operands, and a reference into a packed struct may be misaligned.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"

#include <cstdint>

namespace system_core {
namespace protocols {
namespace aproto {
namespace test {

/// Copies of the header's multi-byte members.
struct HeaderWords {
  std::uint16_t magic;
  std::uint32_t fullUid;
  std::uint16_t opcode;
  std::uint16_t sequence;
  std::uint16_t payloadLength;
};

/// Read the multi-byte members by value, which a packed struct permits.
inline HeaderWords wordsOf(const AprotoHeader& header) {
  return {header.magic, header.fullUid, header.opcode, header.sequence, header.payloadLength};
}

} // namespace test
} // namespace aproto
} // namespace protocols
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INFRASTRUCTURE_PROTOCOLS_APROTO_UTST_HEADER_WORDS_HPP
