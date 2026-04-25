#ifndef APEX_SYSTEM_CORE_EXECUTIVE_EXTERNALIO_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_EXTERNALIO_HPP
/**
 * @file ApexExecutive_ExternalIO.hpp
 * @brief External I/O command handling with non-blocking poll-based input.
 *
 * Optimizations:
 *  - Non-blocking I/O via poll() with timeout (eliminates detach workaround)
 *  - Fixed buffer input (zero heap allocation per command)
 *  - Inline command parsing (reduced function call overhead)
 *  - Statistics tracking (commands processed, errors detected)
 *  - Graceful signal handling (EINTR recovery)
 */

#include <cstdint>
#include <cstring>

#include <atomic>

namespace executive {

/**
 * @enum Command
 * @brief Interactive control commands from external input.
 */
enum class Command : std::uint8_t { NONE = 0, PAUSE, RESUME, FAST_FORWARD, QUIT, UNKNOWN };

/**
 * @struct ExternalIOStats
 * @brief Runtime statistics for external I/O operations.
 */
struct ExternalIOStats {
  std::atomic<std::uint64_t> commandsProcessed{0}; ///< Total commands executed
  std::atomic<std::uint64_t> unknownCommands{0};   ///< Unrecognized input count
  std::atomic<std::uint64_t> emptyLines{0};        ///< Empty line count
  std::atomic<std::uint64_t> pollErrors{0};        ///< Poll error count
  std::atomic<std::uint64_t> readErrors{0};        ///< Read error count
};

/**
 * @brief Parse single-character command efficiently.
 * @param c Character to parse.
 * @return Parsed command type.
 *
 * Optimizations:
 *  - Single character check (no string comparison)
 *  - Inline for zero overhead
 *  - Case-insensitive via OR trick
 */
inline Command parseCommandChar(char c) noexcept {
  // Convert to lowercase via bitwise OR with 0x20
  // ('A' | 0x20 = 'a', already lowercase chars unchanged)
  const char lc = c | 0x20;

  switch (lc) {
  case 'p':
    return Command::PAUSE;
  case 'r':
    return Command::RESUME;
  case 'f':
    return Command::FAST_FORWARD;
  case 'q':
    return Command::QUIT;
  default:
    return Command::UNKNOWN;
  }
}

/**
 * @brief Trim whitespace from buffer in-place (modifies buffer).
 * @param buf Buffer to trim.
 * @param len Current length.
 * @return New length after trimming.
 *
 * Optimizations:
 *  - In-place operation (no allocation)
 *  - Single pass for leading/trailing whitespace
 *  - Returns new length for efficiency
 */
inline std::size_t trimWhitespace(char* buf, std::size_t len) noexcept {
  if (len == 0)
    return 0;

  // Trim trailing whitespace
  while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' || buf[len - 1] == '\r' ||
                     buf[len - 1] == '\n')) {
    --len;
  }
  buf[len] = '\0';

  // Trim leading whitespace
  std::size_t start = 0;
  while (start < len && (buf[start] == ' ' || buf[start] == '\t')) {
    ++start;
  }

  if (start > 0) {
    std::memmove(buf, buf + start, len - start + 1); // +1 for null terminator
    len -= start;
  }

  return len;
}

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_EXTERNALIO_HPP