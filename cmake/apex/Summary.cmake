# ==============================================================================
# apex/Summary.cmake - Configure-time summary banner
# ==============================================================================

include_guard(GLOBAL)

option(APEX_SUMMARY_VERBOSE "Print extra details in configure summary" OFF)

# ------------------------------------------------------------------------------
# Internal helpers
# ------------------------------------------------------------------------------

function (_apex_dumpmachine _exe _out_triple)
  if (NOT _exe)
    set(${_out_triple}
        ""
        PARENT_SCOPE
    )
    return()
  endif ()
  execute_process(
    COMMAND "${_exe}" -dumpmachine
    OUTPUT_VARIABLE _triple
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(${_out_triple}
      "${_triple}"
      PARENT_SCOPE
  )
endfunction ()

function (_apex_compiler_info _lang _out_prefix)
  if (_lang STREQUAL "C")
    set(_exe "${CMAKE_C_COMPILER}")
    set(_ver "${CMAKE_C_COMPILER_VERSION}")
  elseif (_lang STREQUAL "CXX")
    set(_exe "${CMAKE_CXX_COMPILER}")
    set(_ver "${CMAKE_CXX_COMPILER_VERSION}")
  elseif (_lang STREQUAL "CUDA")
    set(_exe "${CMAKE_CUDA_COMPILER}")
    set(_ver "${CMAKE_CUDA_COMPILER_VERSION}")
  else ()
    set(_exe "")
    set(_ver "")
  endif ()

  if (_lang STREQUAL "CUDA")
    _apex_dumpmachine("${CMAKE_CUDA_HOST_COMPILER}" _triple)
  else ()
    _apex_dumpmachine("${_exe}" _triple)
  endif ()

  set(${_out_prefix}_EXE
      "${_exe}"
      PARENT_SCOPE
  )
  set(${_out_prefix}_VERSION
      "${_ver}"
      PARENT_SCOPE
  )
  set(${_out_prefix}_TRIPLE
      "${_triple}"
      PARENT_SCOPE
  )
endfunction ()

function (_apex_join _list_var _sep _out_str)
  set(_tmp "${${_list_var}}")
  if (NOT _tmp)
    set(${_out_str}
        ""
        PARENT_SCOPE
    )
    return()
  endif ()
  string(REPLACE ";" "${_sep}" _joined "${_tmp}")
  set(${_out_str}
      "${_joined}"
      PARENT_SCOPE
  )
endfunction ()

function (_apex_yesno _val _out_str)
  if (_val)
    set(${_out_str}
        "YES"
        PARENT_SCOPE
    )
  else ()
    set(${_out_str}
        "NO"
        PARENT_SCOPE
    )
  endif ()
endfunction ()

function (_apex_tick _cond _out_str)
  if (_cond)
    set(${_out_str}
        "[OK]"
        PARENT_SCOPE
    )
  else ()
    set(${_out_str}
        "[FAIL]"
        PARENT_SCOPE
    )
  endif ()
endfunction ()

function (_apex_contains _str _sub _out_bool)
  if (_str STREQUAL "")
    set(${_out_bool}
        FALSE
        PARENT_SCOPE
    )
    return()
  endif ()
  string(FIND "${_str}" "${_sub}" _idx)
  if (_idx GREATER -1)
    set(${_out_bool}
        TRUE
        PARENT_SCOPE
    )
  else ()
    set(${_out_bool}
        FALSE
        PARENT_SCOPE
    )
  endif ()
endfunction ()

function (_apex_expected_triple _out_triple)
  if (CMAKE_C_COMPILER_TARGET)
    set(_exp "${CMAKE_C_COMPILER_TARGET}")
  elseif (CMAKE_LIBRARY_ARCHITECTURE)
    set(_exp "${CMAKE_LIBRARY_ARCHITECTURE}")
  elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(_exp "aarch64-linux-gnu")
  else ()
    set(_exp "")
  endif ()
  set(${_out_triple}
      "${_exp}"
      PARENT_SCOPE
  )
endfunction ()

function (_apex_cuda_target_libdir _out_libdir _out_root)
  set(_root "")
  set(_libdir "")

  if (TARGET CUDA::cudart)
    get_target_property(_cudart_loc CUDA::cudart IMPORTED_LOCATION)
    if (NOT _cudart_loc)
      get_target_property(_cudart_loc CUDA::cudart LOCATION)
    endif ()
    if (_cudart_loc)
      get_filename_component(_libdir "${_cudart_loc}" DIRECTORY)
    endif ()
  endif ()

  if (NOT _libdir
      AND DEFINED CUDAToolkit_LIBRARY_DIR
      AND CUDAToolkit_LIBRARY_DIR
  )
    set(_libdir "${CUDAToolkit_LIBRARY_DIR}")
  endif ()

  if (DEFINED CUDAToolkit_ROOT AND IS_DIRECTORY "${CUDAToolkit_ROOT}")
    set(_root "${CUDAToolkit_ROOT}")
  elseif (_libdir)
    if (_libdir MATCHES ".*/targets/[^/]+/lib(64)?$")
      get_filename_component(_root "${_libdir}/../.." REALPATH)
    else ()
      get_filename_component(_root "${_libdir}/.." REALPATH)
    endif ()
  endif ()

  set(${_out_libdir}
      "${_libdir}"
      PARENT_SCOPE
  )
  set(${_out_root}
      "${_root}"
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_print_config_summary()
#
# Emit configuration banner showing build settings, compilers, and CUDA status.
# ------------------------------------------------------------------------------
function (apex_print_config_summary)
  # Build type
  if (CMAKE_CONFIGURATION_TYPES)
    _apex_join(CMAKE_CONFIGURATION_TYPES ", " _cfgs)
    set(_build_type "Multi-config [${_cfgs}]")
  elseif (CMAKE_BUILD_TYPE)
    set(_build_type "${CMAKE_BUILD_TYPE}")
  else ()
    set(_build_type "(default)")
  endif ()

  # Cross-compilation
  _apex_yesno("${CMAKE_CROSSCOMPILING}" _is_cross)
  set(_toolchain "${CMAKE_TOOLCHAIN_FILE}")
  if (NOT _toolchain)
    set(_toolchain "(none)")
  endif ()

  # Compilers
  _apex_compiler_info("C" CINFO)
  _apex_compiler_info("CXX" CXXINFO)
  if (CMAKE_CUDA_COMPILER)
    _apex_compiler_info("CUDA" CUDAINFO)
  endif ()

  _apex_expected_triple(_expected_triple)

  # Triple verification (only relevant for cross-compilation)
  set(_c_tick "")
  set(_cxx_tick "")
  set(_cuda_host_tick "")
  if (CMAKE_CROSSCOMPILING AND _expected_triple)
    set(_c_ok TRUE)
    set(_cxx_ok TRUE)
    set(_cuda_host_ok TRUE)
    _apex_contains("${CINFO_TRIPLE}" "${_expected_triple}" _c_ok)
    _apex_contains("${CXXINFO_TRIPLE}" "${_expected_triple}" _cxx_ok)
    if (CMAKE_CUDA_HOST_COMPILER)
      _apex_dumpmachine("${CMAKE_CUDA_HOST_COMPILER}" _cuda_host_triple)
      _apex_contains("${_cuda_host_triple}" "${_expected_triple}" _cuda_host_ok)
    endif ()
    _apex_tick(${_c_ok} _c_tick)
    _apex_tick(${_cxx_ok} _cxx_tick)
    _apex_tick(${_cuda_host_ok} _cuda_host_tick)
  endif ()

  # CUDA status
  set(_cuda_enabled FALSE)
  if (CMAKE_CUDA_COMPILER)
    set(_cuda_enabled TRUE)
  endif ()

  set(_cuda_runtime "${CMAKE_CUDA_RUNTIME_LIBRARY}")
  if (NOT _cuda_runtime)
    if (DEFINED CUDA_USE_STATIC_CUDA_RUNTIME AND CUDA_USE_STATIC_CUDA_RUNTIME)
      set(_cuda_runtime "Static")
    else ()
      set(_cuda_runtime "(default)")
    endif ()
  endif ()

  set(_cuda_root_shown "")
  set(_cuda_libdir "")
  set(_cudart_found FALSE)
  set(_cudadevrt_found FALSE)

  if (_cuda_enabled AND CUDAToolkit_FOUND)
    _apex_cuda_target_libdir(_cuda_libdir _cuda_root)
    if (NOT _cuda_root)
      set(_cuda_root_shown "(unknown)")
    else ()
      set(_cuda_root_shown "${_cuda_root}")
    endif ()

    if (TARGET CUDA::cudart)
      set(_cudart_found TRUE)
    elseif (_cuda_libdir AND EXISTS "${_cuda_libdir}/libcudart.so")
      set(_cudart_found TRUE)
    endif ()

    if (TARGET CUDA::cudadevrt)
      set(_cudadevrt_found TRUE)
    elseif (_cuda_libdir AND EXISTS "${_cuda_libdir}/libcudadevrt.a")
      set(_cudadevrt_found TRUE)
    endif ()
  endif ()

  _apex_tick(${_cudart_found} _cudart_tick)
  _apex_tick(${_cudadevrt_found} _cudadevrt_tick)

  # Linker flags (cross-compile only)
  set(_l_tick "")
  set(_rpath_tick "")
  if (CMAKE_CROSSCOMPILING)
    set(_exe_link_flags "${CMAKE_EXE_LINKER_FLAGS}")
    set(_rpathlink_ok FALSE)
    set(_l_ok FALSE)
    if (_cuda_libdir AND _exe_link_flags)
      _apex_contains("${_exe_link_flags}" "-Wl,-rpath-link,${_cuda_libdir}" _rpathlink_ok)
      _apex_contains("${_exe_link_flags}" "-L${_cuda_libdir}" _l_ok)
    endif ()
    _apex_tick(${_l_ok} _l_tick)
    _apex_tick(${_rpathlink_ok} _rpath_tick)
  else ()
    set(_l_tick "n/a")
    set(_rpath_tick "n/a")
  endif ()

  # Feature toggles
  _apex_yesno("${APEX_REQUIRE_CUDA}" _req_cuda)
  _apex_yesno("${ENABLE_UPX}" _enable_upx)
  _apex_yesno("${ENABLE_COVERAGE}" _enable_cov)
  _apex_yesno("${ENABLE_CLANG_TIDY}" _enable_tidy)

  # Paths
  set(_sysroot "${CMAKE_SYSROOT}")
  if (NOT _sysroot)
    set(_sysroot "(none)")
  endif ()

  _apex_join(CMAKE_PREFIX_PATH " ; " _ppath)
  _apex_join(CMAKE_LIBRARY_PATH " ; " _libpath)

  set(_multiarch "${CMAKE_LIBRARY_ARCHITECTURE}")
  if (NOT _multiarch)
    set(_multiarch "(none)")
  endif ()

  set(_build_rpath "${CMAKE_BUILD_RPATH}")
  if (NOT _build_rpath)
    set(_build_rpath "(none)")
  endif ()

  set(_install_rpath "${CMAKE_INSTALL_RPATH}")
  if (NOT _install_rpath)
    set(_install_rpath "(none)")
  endif ()

  _apex_yesno("${CMAKE_BUILD_WITH_INSTALL_RPATH}" _use_install_rpath)

  set(_emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
  if (NOT _emulator)
    set(_emulator "(none)")
  endif ()

  # Parallelism
  execute_process(
    COMMAND nproc --all
    OUTPUT_VARIABLE _detected_n
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    RESULT_VARIABLE _nproc_rc
  )
  if (NOT _nproc_rc EQUAL 0 OR NOT _detected_n)
    include(ProcessorCount)
    processorcount(_detected_n)
  endif ()
  if (NOT _detected_n)
    set(_detected_n 0)
  endif ()

  set(_parallel "${CMAKE_BUILD_PARALLEL_LEVEL}")
  if (NOT _parallel)
    set(_parallel "(not set)")
  endif ()

  # C++ standard
  if (APEX_PLATFORM_BAREMETAL)
    set(_std "${CMAKE_CXX_STANDARD} (bare-metal)")
  else ()
    set(_std "${CMAKE_CXX_STANDARD}")
  endif ()

  if (CMAKE_CXX_EXTENSIONS)
    set(_exts "ON")
  else ()
    set(_exts "OFF")
  endif ()

  if (DEFINED BUILD_SHARED_LIBS)
    if (BUILD_SHARED_LIBS)
      set(_shared "ON")
    else ()
      set(_shared "OFF")
    endif ()
  else ()
    set(_shared "(default)")
  endif ()

  # Preset name
  set(_preset_name "")
  if (CMAKE_BUILD_PRESET)
    set(_preset_name "${CMAKE_BUILD_PRESET}")
  elseif (DEFINED CMAKE_PRESET AND CMAKE_PRESET)
    set(_preset_name "${CMAKE_PRESET}")
  endif ()

  # Banner output
  message(STATUS "-------------------------------------------------------------------------------")
  message(STATUS "Apex CSF C++ - Configure Summary")
  message(STATUS "-------------------------------------------------------------------------------")
  message(
    STATUS
      "Project          : ${PROJECT_NAME}        CMake ${CMAKE_VERSION}  Generator: ${CMAKE_GENERATOR}"
  )
  if (_preset_name)
    message(STATUS "Preset/Build Dir : ${_preset_name}  ${CMAKE_BINARY_DIR}")
  else ()
    message(STATUS "Build Dir        : ${CMAKE_BINARY_DIR}")
  endif ()
  message(STATUS "Build Type       : ${_build_type}")
  message(STATUS "Parallel Build   : ${_parallel} (detected: ${_detected_n})")
  if (APEX_STATIC_LINK)
    set(_link_mode "STATIC (APEX_STATIC_LINK=ON, RELOAD_LIBRARY disabled)")
  elseif (BUILD_SHARED_LIBS)
    set(_link_mode "SHARED (RELOAD_LIBRARY enabled)")
  else ()
    set(_link_mode "STATIC")
  endif ()
  message(STATUS "C++ Standard     : ${_std} (extensions: ${_exts})   Linking: ${_link_mode}")
  message(STATUS "")
  message(
    STATUS
      "Target vs Host   : Target=${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}   Cross=${_is_cross}"
  )
  message(STATUS "Toolchain        : ${_toolchain}")
  message(STATUS "Prefix Path      : ${_ppath}")
  message(STATUS "Sysroot          : ${_sysroot}")
  message(STATUS "Try-Compile Mode : ${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  message(STATUS "")
  message(STATUS "Compilers")
  message(
    STATUS
      "  C              : ${CINFO_EXE}  (ver: ${CINFO_VERSION}, triple: ${CINFO_TRIPLE})  ${_c_tick}"
  )
  message(
    STATUS
      "  C++            : ${CXXINFO_EXE} (ver: ${CXXINFO_VERSION}, triple: ${CXXINFO_TRIPLE})  ${_cxx_tick}"
  )
  if (_cuda_enabled)
    message(STATUS "  CUDA Lang      : ENABLED (CUDA ${CUDAToolkit_VERSION})")
    message(STATUS "  CUDA Compiler  : ${CUDAINFO_EXE} (ver: ${CUDAINFO_VERSION})")
    if (CMAKE_CUDA_HOST_COMPILER)
      message(
        STATUS
          "  CUDA Host C++  : ${CMAKE_CUDA_HOST_COMPILER}  (triple: ${_cuda_host_triple})  ${_cuda_host_tick}"
      )
    endif ()
    if (CMAKE_CUDA_ARCHITECTURES)
      message(STATUS "  SM Archs       : ${CMAKE_CUDA_ARCHITECTURES}")
    endif ()
  else ()
    message(STATUS "  CUDA Lang      : DISABLED")
  endif ()
  message(STATUS "")
  if (_cuda_enabled AND CUDAToolkit_FOUND)
    message(STATUS "CUDA Toolkit")
    message(STATUS "  Root           : ${_cuda_root_shown}")
    message(STATUS "  Libdir         : ${_cuda_libdir}")
    message(STATUS "  Runtime        : ${_cuda_runtime}")
    message(STATUS "  Link Flags     : -L ${_l_tick}   -Wl,-rpath-link ${_rpath_tick}")
    message(STATUS "  libcudart.so   : ${_cudart_tick}")
    message(STATUS "  libcudadevrt.a : ${_cudadevrt_tick}")
    message(STATUS "")
  endif ()
  message(STATUS "Lookup & Paths")
  message(STATUS "  Multiarch      : ${_multiarch}")
  message(STATUS "  Library Paths  : ${_libpath}")
  message(STATUS "  Build RPATH    : ${_build_rpath}")
  message(STATUS "  Install RPATH  : ${_install_rpath}   (use for build: ${_use_install_rpath})")
  message(STATUS "  Emulator       : ${_emulator}")
  message(STATUS "")
  message(STATUS "Toggles")
  message(STATUS "  APEX_REQUIRE_CUDA : ${_req_cuda}")
  message(STATUS "  ENABLE_UPX        : ${_enable_upx}")
  message(STATUS "  ENABLE_COVERAGE   : ${_enable_cov}")
  message(STATUS "  ENABLE_CLANG_TIDY : ${_enable_tidy}")

  # Build acceleration info (if module was included)
  if (DEFINED CMAKE_C_COMPILER_LAUNCHER AND CMAKE_C_COMPILER_LAUNCHER)
    get_filename_component(_cache_name "${CMAKE_C_COMPILER_LAUNCHER}" NAME)
    message(STATUS "")
    message(STATUS "Acceleration")
    message(STATUS "  Compiler Cache : ${_cache_name}")
  endif ()

  message(STATUS "-------------------------------------------------------------------------------")
endfunction ()
