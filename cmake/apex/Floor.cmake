# ==============================================================================
# apex/Floor.cmake - MCU-floor compile checks for tier-S libraries
#
# A BAREMETAL-flagged interface library claims its headers are freestanding-
# clean at the C++17 floor -- but hosted CI builds them at C++23 with the full
# standard library available, so a hosted-only regression (a <vector> include,
# a throw, a C++20-ism) stays invisible until a firmware build runs.
#
# apex_add_floor_check() closes that gap on every PR: it compiles a per-lib
# probe TU with the HOST compiler forced to the floor (C++17, no exceptions,
# no RTTI, no threadsafe statics) as an OBJECT target that is part of ALL.
# The real cross toolchains remain the authority for freestanding-header
# availability; this check enforces the language/feature floor continuously.
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# apex_add_floor_check
#
#   apex_add_floor_check(NAME <lib> SOURCES <probe.cpp> [...])
#
# Creates <lib>_floor17, an OBJECT library compiling the probe sources at the
# MCU floor. Skipped on bare-metal platforms (the toolchain file already
# enforces the floor there) and for non-GNU-like compilers.
# ------------------------------------------------------------------------------
function (apex_add_floor_check)
  cmake_parse_arguments(FC "" "NAME" "SOURCES" ${ARGN})

  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()
  if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    return()
  endif ()

  set(_tgt ${FC_NAME}_floor17)
  add_library(${_tgt} OBJECT ${FC_SOURCES})
  set_target_properties(
    ${_tgt}
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED ON
               CXX_EXTENSIONS OFF
  )
  target_compile_options(${_tgt} PRIVATE -fno-exceptions -fno-rtti -fno-threadsafe-statics)
  # Probes include repo-root-relative tier-S headers only; no target deps so
  # a floor probe can never silently pull a hosted-only usage requirement.
  target_include_directories(${_tgt} PRIVATE ${CMAKE_SOURCE_DIR})
endfunction ()
