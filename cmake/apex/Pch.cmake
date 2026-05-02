include_guard(GLOBAL)

# ==============================================================================
# Pch.cmake - Precompiled-header helpers
# ------------------------------------------------------------------------------
# Reduces per-TU header parse time by precompiling a common STL/test set.
#
# Usage:
#   apex_pch_apply(<target>)        # Common library PCH
#   apex_pch_apply(<target> TEST)   # Common + gtest/gmock PCH
#
# Hooked into apex_add_library and apex_add_gtest by default; opt-out via
# NO_PCH on those calls. Bare-metal targets are skipped automatically since
# the header set assumes a hosted POSIX environment with full <thread>,
# <filesystem>, <chrono>, etc.
#
# Each target gets its own per-target PCH (no REUSE_FROM sharing) to keep
# CMake's compile-flag matching simple. PCH still saves time within a
# target -- TUs in the same library share the precompiled header.
# ==============================================================================

option(APEX_USE_PCH "Use precompiled headers for hosted builds" ON)

# ------------------------------------------------------------------------------
# Header sets
# ------------------------------------------------------------------------------
# Common: STL only, no module-specific (e.g. <openssl/evp.h>) and no
# POSIX-tied (<unistd.h>) headers. Driven by the most-included headers in
# src/ (audited 2026-05-02).
set(APEX_PCH_COMMON_HEADERS
    <algorithm>
    <array>
    <atomic>
    <chrono>
    <cmath>
    <cstddef>
    <cstdint>
    <cstring>
    <functional>
    <memory>
    <optional>
    <string>
    <string_view>
    <thread>
    <type_traits>
    <utility>
    <vector>
)

# Test PCH: common + gtest/gmock. Pulls in 8-10k lines of GTest header.
set(APEX_PCH_TEST_HEADERS ${APEX_PCH_COMMON_HEADERS} <gtest/gtest.h> <gmock/gmock.h>)

# ------------------------------------------------------------------------------
# apex_pch_apply(<target> [TEST])
#
# Apply the project standard PCH set to <target>. No-op when:
#   - APEX_USE_PCH is OFF, or
#   - the build is bare-metal (header set assumes hosted POSIX).
# ------------------------------------------------------------------------------
function (apex_pch_apply _target)
  cmake_parse_arguments(_PCH "TEST" "" "" ${ARGN})

  if (NOT APEX_USE_PCH)
    return()
  endif ()

  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  if (_PCH_TEST)
    target_precompile_headers(${_target} PRIVATE ${APEX_PCH_TEST_HEADERS})
  else ()
    target_precompile_headers(${_target} PRIVATE ${APEX_PCH_COMMON_HEADERS})
  endif ()
endfunction ()
