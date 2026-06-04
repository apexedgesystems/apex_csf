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
#   -DSANITIZER=asan|tsan|ubsan Enable sanitizers
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
find_package(CUDAToolkit QUIET)

if (CUDAToolkit_FOUND AND (NOT CMAKE_CROSSCOMPILING OR DEFINED CMAKE_CUDA_COMPILER))
  # Match nvcc's host compiler to CMAKE_CXX_COMPILER. Without this, nvcc
  # picks gcc by default — mixing C++ and CUDA TUs across two toolchains
  # produces incompatible runtime libraries (notably libasan vs libclang_rt.asan).
  if (NOT DEFINED CMAKE_CUDA_HOST_COMPILER AND CMAKE_CXX_COMPILER)
    set(_apex_cuda_host "${CMAKE_CXX_COMPILER}")

    # nvcc enforces a maximum supported host-compiler version: CUDA toolkits
    # before 13.1 reject clang >= 21. When the C++ compiler is newer than the
    # toolkit accepts, step the CUDA host compiler down to a supported clang
    # (not gcc) so the C++ and CUDA TUs still share one runtime. The wider C++
    # build keeps using CMAKE_CXX_COMPILER unchanged.
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "21"
        AND CUDAToolkit_VERSION VERSION_LESS "13.1"
    )
      find_program(APEX_CUDA_HOST_CLANG NAMES clang++-20)
      if (APEX_CUDA_HOST_CLANG)
        set(_apex_cuda_host "${APEX_CUDA_HOST_CLANG}")
        message(STATUS "CUDA ${CUDAToolkit_VERSION} does not support clang "
                       "${CMAKE_CXX_COMPILER_VERSION}; using ${_apex_cuda_host} "
                       "as the CUDA host compiler (C++ build stays on " "${CMAKE_CXX_COMPILER})"
        )
      else ()
        message(WARNING "CUDA ${CUDAToolkit_VERSION} does not support clang "
                        "${CMAKE_CXX_COMPILER_VERSION} and clang++-20 was not "
                        "found; CUDA configuration may fail"
        )
      endif ()
    endif ()

    set(CMAKE_CUDA_HOST_COMPILER
        "${_apex_cuda_host}"
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
endif ()

# ------------------------------------------------------------------------------
# Sanitizers
# ------------------------------------------------------------------------------
set(SANITIZER
    ""
    CACHE STRING "Enable sanitizer: asan | tsan | ubsan"
)

if (SANITIZER
    AND NOT CMAKE_CROSSCOMPILING
    AND NOT APEX_PLATFORM_BAREMETAL
)
  if (SANITIZER STREQUAL "asan")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fno-omit-frame-pointer)
  elseif (SANITIZER STREQUAL "tsan")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
  elseif (SANITIZER STREQUAL "ubsan")
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  endif ()
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
