#ifndef APEX_UTILITIES_BUILD_INFO_BUILD_INFO_HPP
#define APEX_UTILITIES_BUILD_INFO_BUILD_INFO_HPP
/**
 * @file BuildInfo.hpp
 * @brief Identity of the build a binary came from: source state and configuration.
 *
 * The values are generated into this library's one translation unit on every
 * build, from the git state of the source tree and the configuration CMake
 * was given, and rewritten only when they change. An executive logs summary()
 * at boot, so every run log names the source state behind the binaries in its
 * bank.
 *
 * The strings are a function of source state and configuration alone (no
 * build time), so two builds of the same tree and configuration agree. Tree
 * state covers the paths git tracks, staged and intent-to-add additions
 * included: an untracked file changes nothing here.
 *
 * Where git or the repository metadata is unavailable (a source archive, an
 * image build context without .git), the git-derived fields read "unknown".
 *
 * The string accessors return pointers to static storage and dirty() a bool;
 * all are RT-safe. copySummary() reads the summary line into a caller's
 * buffer, whole or in windows, and is the surface on AVR, where the summary
 * lives in program memory and the pointer accessors are not declared.
 */

#include "src/utilities/compatibility/inc/compat_rt_attrs.hpp"

#include <stddef.h>

namespace apex {
namespace build_info {

/// Length of the summary line, without its terminator.
[[nodiscard]] size_t summaryLength() noexcept APEX_NONBLOCKING;

/**
 * @brief Copy the summary line, or a window of it, into a caller's buffer.
 *
 * Copies up to cap bytes starting at offset, with no terminator, and returns
 * the number copied: zero at or past the end, for a zero cap or a null
 * buffer. A caller with a small buffer reads the line in windows by
 * advancing offset. This is the way to read the summary where it lives in
 * program memory (AVR), and it works the same everywhere.
 */
[[nodiscard]] size_t copySummary(char* out, size_t cap,
                                 size_t offset = 0) noexcept APEX_NONBLOCKING;

#if !defined(__AVR__)

/**
 * @brief The whole identity on one line.
 *
 * "<project> <describe> (<branch>) tree=<state> <build type> <compiler> <system>",
 * for example
 * "apex_csf v0.0.1-165-g7c9d744 (main) tree=clean Release Clang-21.1.8 Linux/x86_64".
 */
[[nodiscard]] const char* summary() noexcept APEX_NONBLOCKING;

/// CMake project name.
[[nodiscard]] const char* project() noexcept APEX_NONBLOCKING;

/// CMake project version.
[[nodiscard]] const char* version() noexcept APEX_NONBLOCKING;

/// `git describe --tags --always --abbrev=12`, with "-dirty" appended when
/// dirty() is true, or "unknown".
[[nodiscard]] const char* describe() noexcept APEX_NONBLOCKING;

/// HEAD as twelve hex digits, or "unknown".
[[nodiscard]] const char* commit() noexcept APEX_NONBLOCKING;

/// Branch name, "HEAD" when detached, or "unknown".
[[nodiscard]] const char* branch() noexcept APEX_NONBLOCKING;

/**
 * @brief "clean", twelve hex digits, or "unknown".
 *
 * The digits start the id of the tree git would commit from the working
 * tree's tracked paths (every path, mode and object type, under git's own
 * content normalization), built in a temporary index filled from the
 * checkout's own index, which is read and never written; "clean" when
 * that tree is HEAD's; "unknown" in a sparse checkout. Diff settings, path quoting and textconv
 * drivers play no part, so two trees with the same modifications report the same state and two
 * different modifications differ.
 */
[[nodiscard]] const char* treeState() noexcept APEX_NONBLOCKING;

/// Configuration built (CMAKE_BUILD_TYPE), or "unspecified".
[[nodiscard]] const char* buildType() noexcept APEX_NONBLOCKING;

/// "<id>-<version>", for example "Clang-21.1.8".
[[nodiscard]] const char* compiler() noexcept APEX_NONBLOCKING;

/// "<system>/<processor>" on hosted builds, for example "Linux/x86_64";
/// "<hal platform>/<processor>[/<board>]" on firmware, for example
/// "stm32/arm/nucleo_l476rg".
[[nodiscard]] const char* targetSystem() noexcept APEX_NONBLOCKING;

#endif // !__AVR__

/// True when treeState() names a tree other than HEAD's; false for "clean"
/// and "unknown".
[[nodiscard]] bool dirty() noexcept APEX_NONBLOCKING;

} // namespace build_info
} // namespace apex

#endif // APEX_UTILITIES_BUILD_INFO_BUILD_INFO_HPP
