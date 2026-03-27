#ifndef APEX_UTILITIES_HELPERS_ARGS_HPP
#define APEX_UTILITIES_HELPERS_ARGS_HPP
/**
 * @file Args.hpp
 * @brief CLI argument parsing utilities.
 *
 * Provides fixed-arity argument parsing for CLI tools. Cold-path only.
 *
 * @note Cold-path: Allocates std::unordered_map for parsed results.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>

namespace apex {
namespace helpers {
namespace args {

/* ----------------------------- Types ----------------------------- */

/**
 * @brief Definition for a CLI argument flag.
 */
struct ArgDef {
  std::string_view flag;   ///< Flag string, e.g. "--foo"
  std::uint8_t nargs;      ///< Number of values required after the flag
  bool required;           ///< True if flag must be provided
  std::string_view desc{}; ///< Description for help output (optional)
};

/// Map from key to argument definition.
using ArgMap = std::unordered_map<std::uint8_t, ArgDef>;

/// Map from key to parsed values.
using ParsedArgs = std::unordered_map<std::uint8_t, std::vector<std::string_view>>;

namespace detail {

/// Append unsigned int to string without fmt/iostreams (cold path).
inline void appendUint(std::string& s, unsigned int x) {
  std::array<char, 12> buf{};
  char* p = buf.data() + buf.size();
  *--p = '\0';
  do {
    *--p = static_cast<char>('0' + (x % 10));
    x /= 10;
  } while (x);
  s.append(p);
}

inline void appendKeyPair(std::string& s, const char* k, std::uint8_t v) {
  s.append(k);
  s.push_back('=');
  s.push_back('\'');
  appendUint(s, static_cast<unsigned int>(v));
  s.push_back('\'');
}

/// Compact, parse-ready view of an argument definition.
struct ArgDefView {
  std::uint8_t key;
  std::uint8_t need;
  bool required;
  std::string_view flag;
};

} // namespace detail

/* ----------------------------- API ----------------------------- */

/**
 * @brief Parse user-provided arguments according to a flag map.
 *
 * Fixed-arity parser: when a flag is matched, it consumes the next nargs tokens
 * literally as its values.
 *
 * @param args   Argument list (non-owning views; must outlive the call).
 * @param map    Definitions of accepted flags and their requirements.
 * @param pargs  Output map of parsed values (entries are overwritten per key).
 * @param error  Optional error message target (set on failure when provided).
 * @return true on success; false on error (and sets error if provided).
 * @note Cold-path: Allocates internally.
 */
[[nodiscard]] inline bool
parseArgs(apex::compat::rospan<std::string_view> args, const ArgMap& map, ParsedArgs& pargs,
          std::optional<std::reference_wrapper<std::string>> error = std::nullopt) noexcept {
  const std::size_t N = args.size();
  if (N == 0) {
    if (error) {
      error->get() = "No arguments provided";
    }
    return false;
  }

  // Build reverse LUT once: flag -> compact view
  std::unordered_map<std::string_view, detail::ArgDefView> lut;
  lut.reserve(map.size());
  for (const auto& KV : map) {
    const std::uint8_t KEY = KV.first;
    const ArgDef& DEF = KV.second;
    lut.emplace(DEF.flag, detail::ArgDefView{KEY, DEF.nargs, DEF.required, DEF.flag});
  }

  std::bitset<256> seen;
  const std::string_view* const ARGV = args.data();

  for (std::size_t i = 0; i < N; ++i) {
    const std::string_view TOK = ARGV[i];
    auto it = lut.find(TOK);
    if (it == lut.end()) {
      continue;
    }

    const detail::ArgDefView& D = it->second;

    // Need tokens in [i+1, i+D.need]
    if (i + static_cast<std::size_t>(D.need) >= N) {
      if (error) {
        std::string& e = error->get();
        e.assign("Argument out of bounds: expected ");
        detail::appendUint(e, D.need);
        e.append(" values for flag '");
        e.append(D.flag);
        e.push_back('\'');
      }
      return false;
    }

    auto emplaceRes = pargs.try_emplace(D.key);
    auto& out = emplaceRes.first->second;
    out.clear();
    out.reserve(D.need);
    for (std::uint8_t k = 0; k < D.need; ++k) {
      out.emplace_back(ARGV[i + 1 + k]);
    }

    seen.set(D.key);
    i += D.need;
  }

  // Validate required flags
  for (const auto& KV : map) {
    const std::uint8_t KEY = KV.first;
    const ArgDef& DEF = KV.second;
    if (DEF.required && !seen.test(KEY)) {
      if (error) {
        std::string& e = error->get();
        e.assign("Missing required argument: ");
        detail::appendKeyPair(e, "key", KEY);
        e.append(", flag='");
        e.append(DEF.flag);
        e.push_back('\'');
      }
      return false;
    }
  }

  return true;
}

/**
 * @brief Print usage information for a CLI tool.
 *
 * Generates formatted help text from the argument map.
 *
 * @param progName    Program name (typically argv[0]).
 * @param description Brief description of the tool's purpose.
 * @param map         Argument definitions to document.
 * @note Cold-path: Performs I/O.
 */
inline void printUsage(const char* progName, std::string_view description,
                       const ArgMap& map) noexcept {
  fmt::print("Usage: {} [OPTIONS]\n\n", progName);

  if (!description.empty()) {
    fmt::print("{}\n\n", description);
  }

  fmt::print("Options:\n");

  // Collect and sort flags for consistent output
  std::vector<std::pair<std::string_view, const ArgDef*>> entries;
  entries.reserve(map.size());
  for (const auto& KV : map) {
    entries.emplace_back(KV.second.flag, &KV.second);
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  // Compute column width for alignment
  std::size_t maxFlagWidth = 0;
  for (const auto& ENTRY : entries) {
    const ArgDef& DEF = *ENTRY.second;
    std::size_t width = DEF.flag.size();
    if (DEF.nargs > 1) {
      width += 12; // " <value> ..."
    } else if (DEF.nargs == 1) {
      width += 8; // " <value>"
    }
    if (width > maxFlagWidth) {
      maxFlagWidth = width;
    }
  }

  // Minimum padding and cap
  if (maxFlagWidth < 16) {
    maxFlagWidth = 16;
  }
  if (maxFlagWidth > 30) {
    maxFlagWidth = 30;
  }

  for (const auto& ENTRY : entries) {
    const ArgDef& DEF = *ENTRY.second;

    // Build flag portion
    std::string flagStr;
    flagStr.reserve(32);
    flagStr.append(DEF.flag);
    if (DEF.nargs > 1) {
      flagStr.append(" <value> ...");
    } else if (DEF.nargs == 1) {
      flagStr.append(" <value>");
    }

    // Print with alignment
    fmt::print("  {:<{}}  ", flagStr, maxFlagWidth);

    // Print description if present
    if (!DEF.desc.empty()) {
      fmt::print("{}", DEF.desc);
    }

    // Append required marker
    if (DEF.required) {
      if (!DEF.desc.empty()) {
        fmt::print(" ");
      }
      fmt::print("(required)");
    }

    fmt::print("\n");
  }
}

} // namespace args
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_ARGS_HPP
