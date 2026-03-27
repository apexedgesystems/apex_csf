#ifndef APEX_UTILITIES_HELPERS_FORMAT_HPP
#define APEX_UTILITIES_HELPERS_FORMAT_HPP
/**
 * @file Format.hpp
 * @brief Human-readable formatting utilities for bytes, frequencies, and counts.
 *
 * Provides consistent formatting across CLI tools and diagnostic output.
 * Uses fmt library for string formatting.
 *
 * @note NOT RT-SAFE: All functions return std::string (heap allocation).
 *       Use only in cold paths (CLI output, logging, etc.).
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <fmt/format.h>

namespace apex {
namespace helpers {
namespace format {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Format bytes using binary units (KiB, MiB, GiB, TiB).
 * @param bytes Byte count.
 * @return Formatted string (e.g., "1.5 GiB").
 * @note NOT RT-SAFE: Returns std::string.
 */
[[nodiscard]] inline std::string bytesBinary(std::uint64_t bytes) {
  if (bytes == 0) {
    return "0 B";
  }

  static constexpr std::uint64_t KIB = 1024ULL;
  static constexpr std::uint64_t MIB = KIB * 1024ULL;
  static constexpr std::uint64_t GIB = MIB * 1024ULL;
  static constexpr std::uint64_t TIB = GIB * 1024ULL;

  if (bytes >= TIB) {
    return fmt::format("{:.1f} TiB", static_cast<double>(bytes) / static_cast<double>(TIB));
  }
  if (bytes >= GIB) {
    return fmt::format("{:.1f} GiB", static_cast<double>(bytes) / static_cast<double>(GIB));
  }
  if (bytes >= MIB) {
    return fmt::format("{:.1f} MiB", static_cast<double>(bytes) / static_cast<double>(MIB));
  }
  if (bytes >= KIB) {
    return fmt::format("{:.1f} KiB", static_cast<double>(bytes) / static_cast<double>(KIB));
  }

  return fmt::format("{} B", bytes);
}

/**
 * @brief Format bytes using decimal units (KB, MB, GB, TB).
 * @param bytes Byte count.
 * @return Formatted string (e.g., "1.5 GB").
 * @note NOT RT-SAFE: Returns std::string.
 */
[[nodiscard]] inline std::string bytesDecimal(std::uint64_t bytes) {
  if (bytes == 0) {
    return "0 B";
  }

  static constexpr std::uint64_t KB = 1000ULL;
  static constexpr std::uint64_t MB = KB * 1000ULL;
  static constexpr std::uint64_t GB = MB * 1000ULL;
  static constexpr std::uint64_t TB = GB * 1000ULL;

  if (bytes >= TB) {
    return fmt::format("{:.1f} TB", static_cast<double>(bytes) / static_cast<double>(TB));
  }
  if (bytes >= GB) {
    return fmt::format("{:.1f} GB", static_cast<double>(bytes) / static_cast<double>(GB));
  }
  if (bytes >= MB) {
    return fmt::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(MB));
  }
  if (bytes >= KB) {
    return fmt::format("{:.1f} KB", static_cast<double>(bytes) / static_cast<double>(KB));
  }

  return fmt::format("{} B", bytes);
}

/**
 * @brief Format frequency in Hz with appropriate unit.
 * @param hz Frequency in Hertz.
 * @return Formatted string (e.g., "2.4 GHz").
 * @note NOT RT-SAFE: Returns std::string.
 */
[[nodiscard]] inline std::string frequencyHz(std::uint64_t hz) {
  if (hz == 0) {
    return "0 Hz";
  }

  static constexpr std::uint64_t KHZ = 1000ULL;
  static constexpr std::uint64_t MHZ = KHZ * 1000ULL;
  static constexpr std::uint64_t GHZ = MHZ * 1000ULL;

  if (hz >= GHZ) {
    return fmt::format("{:.2f} GHz", static_cast<double>(hz) / static_cast<double>(GHZ));
  }
  if (hz >= MHZ) {
    return fmt::format("{:.1f} MHz", static_cast<double>(hz) / static_cast<double>(MHZ));
  }
  if (hz >= KHZ) {
    return fmt::format("{:.1f} kHz", static_cast<double>(hz) / static_cast<double>(KHZ));
  }

  return fmt::format("{} Hz", hz);
}

/**
 * @brief Format count with appropriate suffix (K, M, B).
 * @param count The count to format.
 * @return Formatted string (e.g., "1.2M").
 * @note NOT RT-SAFE: Returns std::string.
 */
[[nodiscard]] inline std::string count(std::uint64_t count) {
  if (count == 0) {
    return "0";
  }

  static constexpr std::uint64_t THOUSAND = 1000ULL;
  static constexpr std::uint64_t MILLION = THOUSAND * 1000ULL;
  static constexpr std::uint64_t BILLION = MILLION * 1000ULL;

  if (count >= BILLION) {
    return fmt::format("{:.1f}B", static_cast<double>(count) / static_cast<double>(BILLION));
  }
  if (count >= MILLION) {
    return fmt::format("{:.1f}M", static_cast<double>(count) / static_cast<double>(MILLION));
  }
  if (count >= THOUSAND) {
    return fmt::format("{:.1f}K", static_cast<double>(count) / static_cast<double>(THOUSAND));
  }

  return fmt::format("{}", count);
}

/* ----------------------------- Debug Output ----------------------------- */

/**
 * @brief Print a byte buffer as space-separated hexadecimal plus newline.
 *
 * No output is produced for empty spans.
 *
 * @param span Read-only view of bytes to print.
 * @note Cold-path: Performs I/O.
 */
inline void printSpan(apex::compat::bytes_span span) noexcept {
  if (span.empty()) {
    return;
  }

  fmt::memory_buffer buffer;
  buffer.reserve(span.size() * 5U + 1U);

  for (std::size_t i = 0; i < span.size(); ++i) {
    const std::uint8_t BYTE = span.data()[i];
    fmt::format_to(std::back_inserter(buffer), FMT_STRING("{:#04x} "), BYTE);
  }

  buffer.push_back('\n');
  fmt::print("{}", std::string_view(buffer.data(), buffer.size()));
}

} // namespace format
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_FORMAT_HPP
