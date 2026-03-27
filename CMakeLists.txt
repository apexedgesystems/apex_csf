cmake_minimum_required(VERSION 3.24)

# ==============================================================================
# Apex CSF C++ - Root CMake
# ==============================================================================
#
# Build examples:
#   cmake --preset native-linux-debug
#   cmake --preset native-linux-release
#   cmake --preset jetson-aarch64-release
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

# Build acceleration (ccache, fast linker, split DWARF)
include(apex/BuildAcceleration)

# Coverage initialization
include(apex/Coverage)
apex_coverage_init()

# ------------------------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------------------------
# APEX_PLATFORM_BAREMETAL: True when targeting bare-metal (no OS)
# Must be set early, before add_subdirectory(src) which checks it
if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
  set(APEX_PLATFORM_BAREMETAL
      TRUE
      CACHE INTERNAL "Bare-metal platform detected"
  )
  message(STATUS "[apex] Bare-metal platform detected")
else ()
  set(APEX_PLATFORM_BAREMETAL
      FALSE
      CACHE INTERNAL "Not a bare-metal platform"
  )
endif ()

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
  enable_language(CUDA)

  set(CUDA_ARCHS
      "89"
      CACHE STRING "CUDA SM architectures"
  )
  set(CMAKE_CUDA_ARCHITECTURES "${CUDA_ARCHS}")
  set(CMAKE_CUDA_STANDARD 20)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)

  option(CUDA_KEEP_INTERMEDIATES "Keep NVCC intermediate files" ON)
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
