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
 * state covers the paths git tracks, staged additions included: an untracked
 * file changes nothing here.
 *
 * Where git or the repository metadata is unavailable (a source archive, an
 * image build context without .git), the git-derived fields read "unknown".
 *
 * The string accessors return pointers to static storage and dirty() a bool;
 * all are RT-safe.
 */

#include "compat_rt_attrs.hpp"

namespace apex {
namespace build_info {

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
 * content normalization), built in a temporary index seeded from the
 * checkout's own, which is read and never written; "clean" when that tree
 * is HEAD's. Diff settings, path quoting and textconv drivers play no part,
 * so two trees with the same modifications report the same state and two
 * different modifications differ.
 */
[[nodiscard]] const char* treeState() noexcept APEX_NONBLOCKING;

/// True when treeState() names a tree other than HEAD's; false for "clean"
/// and "unknown".
[[nodiscard]] bool dirty() noexcept APEX_NONBLOCKING;

/// Configuration built (CMAKE_BUILD_TYPE), or "unspecified".
[[nodiscard]] const char* buildType() noexcept APEX_NONBLOCKING;

/// "<id>-<version>", for example "Clang-21.1.8".
[[nodiscard]] const char* compiler() noexcept APEX_NONBLOCKING;

/// "<system>/<processor>", for example "Linux/x86_64".
[[nodiscard]] const char* targetSystem() noexcept APEX_NONBLOCKING;

} // namespace build_info
} // namespace apex

#endif // APEX_UTILITIES_BUILD_INFO_BUILD_INFO_HPP
