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
 * state covers tracked files only: an untracked file changes nothing here.
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

/// `git describe --tags --always --dirty --abbrev=12`, or "unknown".
[[nodiscard]] const char* describe() noexcept APEX_NONBLOCKING;

/// HEAD as twelve hex digits, or "unknown".
[[nodiscard]] const char* commit() noexcept APEX_NONBLOCKING;

/// Branch name, "HEAD" when detached, or "unknown".
[[nodiscard]] const char* branch() noexcept APEX_NONBLOCKING;

/**
 * @brief "clean", twelve hex digits, or "unknown".
 *
 * The digits start the SHA-256 of a listing of every tracked path whose
 * content or mode differs from HEAD, staged or unstaged alike, each with the
 * raw hash of its working-tree bytes. It comes from git plumbing and blob
 * hashes, not from diff text, so diff settings do not move it and a textconv
 * driver cannot hide an edit: two trees with the same modifications report
 * the same state.
 */
[[nodiscard]] const char* treeState() noexcept APEX_NONBLOCKING;

/// True when treeState() names a diff.
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
