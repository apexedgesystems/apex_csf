cmake_minimum_required(VERSION 3.24)

# ==============================================================================
# Apex CSF C++ - Root CMake
# ==============================================================================
#
# Build examples:
#   cmake --preset hosted-x86_64-debug
#   cmake --preset hosted-x86_64-release
#   cmake --preset cross-jetson-release
#
# Options:
#   -DCUDA_ARCHS="89"           Ada only (fast)
#   -DCUDA_ARCHS="80;86;89"     Ampere + Ada
#   -DENABLE_COVERAGE=ON        Enable coverage (Debug + Clang only)
#   -DENABLE_CLANG_TIDY=ON      Enable clang-tidy
#   -DSANITIZER=asan|tsan|ubsan|rtsan Enable sanitizers
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Project
# ------------------------------------------------------------------------------
project(
  apex_csf
  VERSION 0.0.1
  LANGUAGES C CXX
  DESCRIPTION "Apex computational and systems framework"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Build-tier classification (hosted / cross-linux / baremetal / rtos)
include(apex/Tiers)

# Build acceleration (ccache, fast linker, split DWARF)
include(apex/BuildAcceleration)

# Precompiled headers (hosted builds only; auto-skipped for bare-metal)
include(apex/Pch)

# Coverage initialization
include(apex/Coverage)
apex_coverage_init()

# ------------------------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------------------------
# APEX_PLATFORM_BAREMETAL is a derived alias of APEX_TIER.
apex_tier_is("baremetal" _is_baremetal)
set(APEX_PLATFORM_BAREMETAL
    ${_is_baremetal}
    CACHE INTERNAL "Alias: APEX_TIER STREQUAL baremetal"
)
unset(_is_baremetal)

# APEX_HAL_PLATFORM: Which HAL platform to build (stm32, avr, pico, esp32)
# Set explicitly via preset or -DAPEX_HAL_PLATFORM=stm32
# Auto-detected from toolchain file name when not set
set(APEX_HAL_PLATFORM
    ""
    CACHE STRING "HAL platform: stm32 | avr | pico | esp32 | c2000 (empty = none)"
)
if (APEX_HAL_PLATFORM STREQUAL "" AND CMAKE_TOOLCHAIN_FILE)
  get_filename_component(_tc_name "${CMAKE_TOOLCHAIN_FILE}" NAME)
  if (_tc_name MATCHES "stm32")
    set(APEX_HAL_PLATFORM
        "stm32"
        CACHE STRING "" FORCE
    )
  elseif (_tc_name MATCHES "avr")
    set(APEX_HAL_PLATFORM
        "avr"
        CACHE STRING "" FORCE
    )
  elseif (_tc_name MATCHES "pico")
    set(APEX_HAL_PLATFORM
        "pico"
        CACHE STRING "" FORCE
    )
  elseif (_tc_name MATCHES "esp32")
    set(APEX_HAL_PLATFORM
        "esp32"
        CACHE STRING "" FORCE
    )
  elseif (_tc_name MATCHES "c2000")
    set(APEX_HAL_PLATFORM
        "c2000"
        CACHE STRING "" FORCE
    )
  endif ()
endif ()
if (NOT APEX_HAL_PLATFORM STREQUAL "")
  message(STATUS "[apex] HAL platform: ${APEX_HAL_PLATFORM}")
  string(TOUPPER "${APEX_HAL_PLATFORM}" _HAL_UPPER)
  add_compile_definitions(APEX_PLATFORM_${_HAL_UPPER})
endif ()

# ------------------------------------------------------------------------------
# Language standard
# ------------------------------------------------------------------------------
# Default to C++23. Bare-metal toolchains override via CACHE FORCE in their
# toolchain files (e.g. stm32-gcc.cmake sets C++20, avr-gcc.cmake sets C++17).
if (NOT DEFINED CACHE{CMAKE_CXX_STANDARD})
  set(CMAKE_CXX_STANDARD
      23
      CACHE STRING "C++ standard (toolchains may override)"
  )
endif ()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# TI CGT requires extensions ON (driverlib uses non-standard trailing commas).
# Toolchain sets CACHE FORCE; don't override here.
if (NOT DEFINED CACHE{CMAKE_CXX_EXTENSIONS})
  set(CMAKE_CXX_EXTENSIONS OFF)
endif ()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Warnings (C++ only to avoid NVCC noise, skip for TI compiler)
# ------------------------------------------------------------------------------
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "TI")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
endif ()

# ------------------------------------------------------------------------------
# Shared libraries
# ------------------------------------------------------------------------------
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(APEX_STATIC_LINK "Force all libraries to STATIC (disables RELOAD_LIBRARY)" OFF)
if (APEX_PLATFORM_BAREMETAL OR APEX_STATIC_LINK)
  set(BUILD_SHARED_LIBS
      OFF
      CACHE BOOL "Build shared libraries" FORCE
  )
  if (NOT APEX_PLATFORM_BAREMETAL)
    # Static archives linked into PIE executables require position-independent code.
    # This also covers FetchContent dependencies (fmt, googletest, etc.).
    # Bare-metal targets must NOT use PIC/PIE (no dynamic linker, GOT breaks).
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  endif ()
endif ()

# ------------------------------------------------------------------------------
# Feature toggles
# ------------------------------------------------------------------------------
option(PROJECT_BUILD_DOCS "Enable Doxygen doc targets" ON)
option(ENABLE_UPX "Create UPX-compressed copies" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy integration" OFF)
option(APEX_BUILD_TOOLS "Build CLI tools (Rust, Python, Shell)" ON)

# ------------------------------------------------------------------------------
# CUDA (optional)
# ------------------------------------------------------------------------------
option(APEX_REQUIRE_CUDA "Fail configure if CUDA is required but not found" OFF)

find_package(CUDAToolkit QUIET)

if (CUDAToolkit_FOUND AND (NOT CMAKE_CROSSCOMPILING OR DEFINED CMAKE_CUDA_COMPILER))
  # Match nvcc's host compiler to CMAKE_CXX_COMPILER. Without this, nvcc
  # picks gcc by default — mixing C++ and CUDA TUs across two toolchains
  # produces incompatible runtime libraries (notably libasan vs libclang_rt.asan).
  if (NOT DEFINED CMAKE_CUDA_HOST_COMPILER AND CMAKE_CXX_COMPILER)
    set(CMAKE_CUDA_HOST_COMPILER
        "${CMAKE_CXX_COMPILER}"
        CACHE FILEPATH ""
    )
  endif ()

  enable_language(CUDA)

  set(CUDA_ARCHS
      "89"
      CACHE STRING "CUDA SM architectures"
  )
  set(CMAKE_CUDA_ARCHITECTURES "${CUDA_ARCHS}")
  set(CMAKE_CUDA_STANDARD 20)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)

  # OFF by default: a single shared --keep-dir causes parallel CUDA target
  # builds to race on cmake_device_link.reg.c, producing cross-target
  # __cudaRegisterLinkedBinary / __fatbinwrap link errors. Opt in only when
  # debugging nvcc intermediates.
  option(CUDA_KEEP_INTERMEDIATES "Keep NVCC intermediate files" OFF)
  set(_nvcc_keep "")
  if (CUDA_KEEP_INTERMEDIATES)
    set(NVCC_TMP_DIR "${CMAKE_BINARY_DIR}/nvcc")
    file(MAKE_DIRECTORY "${NVCC_TMP_DIR}")
    set(_nvcc_keep "--keep --keep-dir=${NVCC_TMP_DIR}")
  endif ()

  set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --expt-relaxed-constexpr ${_nvcc_keep}")
  set(CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG} -G")
  set(CMAKE_CUDA_FLAGS_RELWITHDEBINFO "${CMAKE_CUDA_FLAGS_RELWITHDEBINFO} -lineinfo -DNDEBUG")
  set(CMAKE_CUDA_FLAGS_RELEASE "${CMAKE_CUDA_FLAGS_RELEASE} --use_fast_math -DNDEBUG")
  set(CMAKE_CUDA_FLAGS_MINSIZEREL "${CMAKE_CUDA_FLAGS_MINSIZEREL} -DNDEBUG")

  add_compile_definitions($<$<COMPILE_LANGUAGE:CUDA>:FMT_USE_BITINT=0>)
elseif (APEX_REQUIRE_CUDA)
  message(FATAL_ERROR "APEX_REQUIRE_CUDA is ON but no usable CUDA toolkit was found. "
                      "Install the CUDA toolkit or configure with a non-CUDA preset."
  )
endif ()

# ------------------------------------------------------------------------------
# Sanitizers
# ------------------------------------------------------------------------------
# Sanitizers compose as a set: asan -> address, tsan -> thread, ubsan ->
# undefined, rtsan -> realtime. undefined combines with any of the others; the
# illegal pairs are address+thread and realtime+(address|thread), since those
# runtimes use conflicting shadow-memory / interception models. So build the
# -fsanitize argument from the requested set and validate the illegal pairs,
# rather than enumerating combinations.
#
# SANITIZER accepts a ; or , separated list, e.g. "asan" or "asan,ubsan".
set(SANITIZER
    ""
    CACHE STRING "Sanitizers: ; or , separated list of asan | tsan | ubsan | rtsan"
)

if (SANITIZER
    AND NOT CMAKE_CROSSCOMPILING
    AND NOT APEX_PLATFORM_BAREMETAL
)
  string(REPLACE "," ";" _san_req "${SANITIZER}")
  list(REMOVE_DUPLICATES _san_req)

  # Map each requested sanitizer to its -fsanitize check name.
  set(_san_checks "")
  foreach (_s IN LISTS _san_req)
    if (_s STREQUAL "asan")
      list(APPEND _san_checks address)
    elseif (_s STREQUAL "tsan")
      list(APPEND _san_checks thread)
    elseif (_s STREQUAL "ubsan")
      list(APPEND _san_checks undefined)
    elseif (_s STREQUAL "rtsan")
      list(APPEND _san_checks realtime)
    else ()
      message(FATAL_ERROR "SANITIZER: unknown '${_s}' (expected asan, tsan, ubsan, or rtsan)")
    endif ()
  endforeach ()

  if ("address" IN_LIST _san_checks AND "thread" IN_LIST _san_checks)
    message(FATAL_ERROR "SANITIZER: asan and tsan are mutually exclusive")
  endif ()

  # RTSan instruments via function interception that conflicts with the
  # asan/tsan shadow-memory runtimes; keep it in its own build.
  if ("realtime" IN_LIST _san_checks AND ("address" IN_LIST _san_checks OR "thread"
                                          IN_LIST _san_checks))
    message(FATAL_ERROR "SANITIZER: rtsan cannot combine with asan or tsan")
  endif ()

  list(JOIN _san_checks "," _san_fsanitize)
  set(_san_flags "-fsanitize=${_san_fsanitize}")

  # Frame pointers keep stack traces readable under AddressSanitizer.
  if ("address" IN_LIST _san_checks)
    list(APPEND _san_flags -fno-omit-frame-pointer)
  endif ()

  # UBSan gates: undefined behavior aborts (and fails the test) instead of
  # logging and continuing.
  if ("undefined" IN_LIST _san_checks)
    list(APPEND _san_flags -fno-sanitize-recover=undefined)
  endif ()

  add_compile_options(${_san_flags})
  add_link_options(${_san_flags})
  message(STATUS "[Sanitizer] -fsanitize=${_san_fsanitize}")
endif ()

# ------------------------------------------------------------------------------
# Hardened build (defense-in-depth; release-based, native only)
# ------------------------------------------------------------------------------
# Compile- and link-time mitigations that turn latent memory/UB issues into
# immediate, diagnosable failures. _FORTIFY_SOURCE needs optimization, so the
# hardened preset is release-based. Native only -- cross/bare-metal toolchains
# do not provide these consistently. C++ only, matching the warnings policy.
option(HARDENED "Enable hardening flags (FORTIFY, libstdc++ assertions, stack/CF protection)" OFF)
if (HARDENED AND NOT CMAKE_CROSSCOMPILING AND NOT APEX_PLATFORM_BAREMETAL)
  set(_harden_flags
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 # glibc buffer-overflow checks (needs -O)
      -D_GLIBCXX_ASSERTIONS                 # libstdc++ precondition checks (bounds, ...)
      -fstack-protector-strong              # stack canaries
      -fstack-clash-protection              # large-stack probing
      -fcf-protection=full                  # control-flow integrity (CET; no-op pre-CET)
      -ftrivial-auto-var-init=zero          # zero otherwise-uninitialized locals
  )
  # Wrap each flag in its own genex -- a ;-list inside one genex gets split by
  # add_compile_options, mangling the first and last entries.
  foreach (_hf IN LISTS _harden_flags)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${_hf}>)
  endforeach ()
  add_link_options(LINKER:-z,relro LINKER:-z,now)
  message(STATUS "[Hardened] FORTIFY_SOURCE=3, _GLIBCXX_ASSERTIONS, stack/CF protection, auto-var-init")
endif ()

# ------------------------------------------------------------------------------
# Build parallelism and output directories
# ------------------------------------------------------------------------------
execute_process(
  COMMAND nproc --all
  OUTPUT_VARIABLE _N
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
  RESULT_VARIABLE _NPROC_RC
)
if (NOT _NPROC_RC EQUAL 0 OR NOT _N)
  include(ProcessorCount)
  processorcount(_N)
endif ()
if (_N GREATER 0 AND NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
  set(CMAKE_BUILD_PARALLEL_LEVEL ${_N})
endif ()

# Host builds flatten outputs to bin/ and lib/.  Bare-metal builds skip
# this -- SDKs (Pico SDK, ESP-IDF) manage their own library layout, and
# apex_add_firmware() sets RUNTIME_OUTPUT_DIRECTORY to firmware/.
if (NOT APEX_PLATFORM_BAREMETAL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif ()

# ------------------------------------------------------------------------------
# RPATH configuration (host builds only)
# ------------------------------------------------------------------------------
if (NOT APEX_PLATFORM_BAREMETAL)
  set(CMAKE_SKIP_BUILD_RPATH OFF)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
  set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
  set(CMAKE_BUILD_RPATH "$ORIGIN/../lib")
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
endif ()

# ------------------------------------------------------------------------------
# Configure summary
# ------------------------------------------------------------------------------
include(apex/Summary)
apex_print_config_summary()

# ------------------------------------------------------------------------------
# External dependencies
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/ExternalDependencies.cmake" OPTIONAL)

# ------------------------------------------------------------------------------
# Project-wide includes
# ------------------------------------------------------------------------------
include_directories("${PROJECT_SOURCE_DIR}")

# ------------------------------------------------------------------------------
# clang-tidy
# ------------------------------------------------------------------------------
if (ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if (CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else ()
    message(WARNING "ENABLE_CLANG_TIDY=ON but clang-tidy not found")
  endif ()
endif ()

# ------------------------------------------------------------------------------
# Gated warnings (first-party code only)
# ------------------------------------------------------------------------------
# Gate a curated set of warnings as errors -- but only for our own sources, so
# dependency builds are never gated (ExternalDependencies were configured above;
# our subdirs are added below). Each flag gets its own generator expression
# because a semicolon-list inside one $<...> is split by add_compile_options.
#
# The numeric-narrowing classes (implicit-int-conversion / shorten-64-to-32 /
# double-promotion) are gated only after every existing site was reviewed and
# resolved individually -- each cast records why the value is provably in range,
# and the genuinely unbounded CLI parses now clamp-and-warn instead of silently
# truncating. They are NOT silenced wholesale; the gate keeps new truncation
# paths from landing. Domain-inherent noisy classes (sign-conversion,
# float-equal) are left out as they fight legitimate patterns in this tree.
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "TI")
  foreach (_wf IN ITEMS
           -Wshadow
           -Wold-style-cast
           -Wimplicit-int-conversion
           -Wshorten-64-to-32
           -Wdouble-promotion
           -Werror=unused-variable
           -Werror=unused-const-variable
           -Werror=deprecated-volatile
           -Werror=shadow
           -Werror=old-style-cast
           -Werror=implicit-int-conversion
           -Werror=shorten-64-to-32
           -Werror=double-promotion)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${_wf}>)
  endforeach ()
endif ()

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(src)
add_subdirectory(apps)

# Tools require a hosted OS (skip for bare-metal, optional via APEX_BUILD_TOOLS)
if (NOT APEX_PLATFORM_BAREMETAL AND APEX_BUILD_TOOLS)
  add_subdirectory(tools)
endif ()

# ------------------------------------------------------------------------------
# Packaging (package_<APP> targets)
# ------------------------------------------------------------------------------
# Create package_<APP> custom targets. Each target runs pkg_resolve.sh for
# ELF dependency resolution, TPRM staging, and tarball creation.
if (NOT APEX_PLATFORM_BAREMETAL)
  apex_finalize_packages()
endif ()

# ------------------------------------------------------------------------------
# Data Manifests (C2 artifacts)
# ------------------------------------------------------------------------------
# Finalize manifest list after all subdirectories have registered.
# Individual libraries call apex_add_data_manifest() to register.
# Run `make apex-data-db` to generate struct dictionaries.
if (NOT APEX_PLATFORM_BAREMETAL)
  apex_finalize_data_manifests()
endif ()

# ------------------------------------------------------------------------------
# Doxygen (cross-library tag-file resolution)
# ------------------------------------------------------------------------------
# Materializes per-library doxygen targets after every apex_add_doxygen() call
# has registered. Two-pass build: pass 1 produces tag files, pass 2 reads every
# sibling's tag file so cross-module markdown links resolve.
apex_finalize_doxygen()
