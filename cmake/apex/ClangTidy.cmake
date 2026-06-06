# ==============================================================================
# apex/ClangTidy.cmake - clang-tidy for CUDA sources
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

if (NOT DEFINED APEX_CLANG_TIDY_CUDA_STUBS)
  set(APEX_CLANG_TIDY_CUDA_STUBS
      ON
      CACHE BOOL "Generate stub headers for clang-tidy CUDA"
  )
endif ()
if (NOT DEFINED APEX_CLANG_TIDY_CUDA_STUB_HEADERS)
  set(APEX_CLANG_TIDY_CUDA_STUB_HEADERS
      "texture_fetch_functions.h"
      CACHE STRING "CUDA headers to stub (semicolon-separated)"
  )
endif ()
if (NOT DEFINED APEX_CLANG_TIDY_CUDA_MUTE_FRONTEND)
  set(APEX_CLANG_TIDY_CUDA_MUTE_FRONTEND
      ON
      CACHE BOOL "Silence frontend warnings during tidy"
  )
endif ()
if (NOT DEFINED APEX_CLANG_TIDY_CUDA_HEADER_FILTER)
  string(REPLACE "\\" "\\\\" _apex_src_root "${CMAKE_SOURCE_DIR}")
  set(APEX_CLANG_TIDY_CUDA_HEADER_FILTER
      "^${_apex_src_root}/(src|prod)/"
      CACHE STRING "Header filter regex for clang-tidy"
  )
endif ()

# ------------------------------------------------------------------------------
# apex_clang_tidy_cuda(<target> FILES <cu...> [ALLOW_FAILURE])
#
# Run clang-tidy on CUDA sources via Clang's CUDA frontend.
# No-op when CUDA not found, ENABLE_CLANG_TIDY=OFF, or no FILES.
# ------------------------------------------------------------------------------
function (apex_clang_tidy_cuda _target)
  if (NOT TARGET "${_target}")
    return()
  endif ()

  get_target_property(_tgt_type "${_target}" TYPE)
  if (_tgt_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif ()

  if (NOT CUDAToolkit_FOUND)
    return()
  endif ()

  if (DEFINED ENABLE_CLANG_TIDY AND NOT ENABLE_CLANG_TIDY)
    return()
  endif ()

  cmake_parse_arguments(CT "ALLOW_FAILURE" "" "FILES" ${ARGN})
  if (NOT CT_FILES)
    return()
  endif ()

  # Find clang-tidy
  set(_tidy_exe "${CMAKE_CXX_CLANG_TIDY}")
  if (NOT _tidy_exe)
    find_program(_tidy_exe NAMES clang-tidy clang-tidy-21 clang-tidy-20)
  endif ()
  if (NOT _tidy_exe)
    return()
  endif ()

  # CUDA root
  if (DEFINED CUDAToolkit_ROOT)
    set(_cuda_root "${CUDAToolkit_ROOT}")
  elseif (CUDAToolkit_BIN_DIR)
    get_filename_component(_cuda_root "${CUDAToolkit_BIN_DIR}/.." ABSOLUTE)
  else ()
    set(_cuda_root "/usr/local/cuda")
  endif ()

  # C++ standard
  set(_std_flag "")
  if (CMAKE_CUDA_STANDARD)
    set(_std_flag "-std=c++${CMAKE_CUDA_STANDARD}")
  elseif (CMAKE_CXX_STANDARD)
    set(_std_flag "-std=c++${CMAKE_CXX_STANDARD}")
  endif ()

  # SM architecture
  if (DEFINED APEX_CLANG_TIDY_CUDA_ARCH)
    set(_sm "${APEX_CLANG_TIDY_CUDA_ARCH}")
  elseif (CMAKE_CUDA_ARCHITECTURES)
    list(GET CMAKE_CUDA_ARCHITECTURES 0 _sm)
  else ()
    set(_sm "89")
  endif ()

  # Include flags
  set(_iflags)
  get_target_property(_tgt_incs ${_target} INCLUDE_DIRECTORIES)
  foreach (_d IN LISTS _tgt_incs)
    if (_d)
      list(APPEND _iflags "-I\"${_d}\"")
    endif ()
  endforeach ()

  # Stub headers
  if (APEX_CLANG_TIDY_CUDA_STUBS)
    set(_stub_dir "${CMAKE_BINARY_DIR}/clang_tidy_cuda_stubs")
    file(MAKE_DIRECTORY "${_stub_dir}")
    foreach (_hdr IN LISTS APEX_CLANG_TIDY_CUDA_STUB_HEADERS)
      if (NOT _hdr)
        continue()
      endif ()
      get_filename_component(_hdr_base "${_hdr}" NAME)
      set(_dst "${_stub_dir}/${_hdr_base}")
      if (NOT EXISTS "${_dst}")
        file(
          WRITE "${_dst}"
          "// clang-tidy CUDA stub: ${_hdr_base}
#pragma once
#if !defined(__CUDA_ARCH__)
#endif
"
        )
      endif ()
    endforeach ()
    list(INSERT _iflags 0 "-isystem" "\"${_stub_dir}\"")
  endif ()

  # CUDA includes
  set(_cuda_incs)
  if (TARGET CUDA::cudart)
    get_target_property(_cuda_incs CUDA::cudart INTERFACE_INCLUDE_DIRECTORIES)
  endif ()
  if (NOT _cuda_incs)
    set(_cuda_incs ${CUDAToolkit_INCLUDE_DIRS})
  endif ()
  foreach (_cd IN LISTS _cuda_incs)
    if (_cd)
      list(APPEND _iflags "-isystem" "\"${_cd}\"")
    endif ()
  endforeach ()

  # Compile definitions
  set(_dflags)
  get_target_property(_tgt_defs ${_target} COMPILE_DEFINITIONS)
  foreach (_def IN LISTS _tgt_defs)
    if (NOT _def)
      continue()
    endif ()
    if ("${_def}" MATCHES "^-D")
      list(APPEND _dflags "\"${_def}\"")
    else ()
      list(APPEND _dflags "-D\"${_def}\"")
    endif ()
  endforeach ()

  # Compile database
  set(_p_flag "")
  if (EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    set(_p_flag "-p \"${CMAKE_BINARY_DIR}\"")
  endif ()

  # Sysroot for cross-compilation
  set(_sysroot_flag "")
  if (CMAKE_CROSSCOMPILING AND CMAKE_SYSROOT)
    set(_sysroot_flag "--extra-arg=--sysroot=${CMAKE_SYSROOT}")
  endif ()

  set(_filter_re "^[0-9]+ warnings? generated( when compiling for sm_[0-9]+)?\\.")

  foreach (_src IN LISTS CT_FILES)
    if (NOT EXISTS "${_src}")
      continue()
    endif ()

    set(_args)
    list(APPEND _args "-quiet")
    if (_p_flag)
      list(APPEND _args "${_p_flag}")
    endif ()
    list(APPEND _args "--header-filter='${APEX_CLANG_TIDY_CUDA_HEADER_FILTER}'")
    if (APEX_CLANG_TIDY_CUDA_MUTE_FRONTEND)
      list(APPEND _args "--extra-arg=-Wno-everything")
    endif ()
    if (_sysroot_flag)
      list(APPEND _args "${_sysroot_flag}")
    endif ()
    list(APPEND _args "\"${_src}\"" "--" "-x" "cuda")
    if (_std_flag)
      list(APPEND _args "${_std_flag}")
    endif ()
    list(APPEND _args "--cuda-path=${_cuda_root}" "--cuda-gpu-arch=sm_${_sm}")
    list(APPEND _args ${_iflags} ${_dflags})

    string(JOIN " " _joined ${_tidy_exe} ${_args})
    set(_bash_cmd "set -o pipefail; ${_joined} 2>&1 | (grep -v -E '${_filter_re}' || :)")

    if (CT_ALLOW_FAILURE)
      add_custom_command(
        TARGET ${_target}
        POST_BUILD
        COMMAND /usr/bin/env bash -lc "${_bash_cmd} ; exit 0"
        COMMENT "clang-tidy (CUDA) ${_src}"
        VERBATIM
      )
    else ()
      add_custom_command(
        TARGET ${_target}
        POST_BUILD
        COMMAND /usr/bin/env bash -lc "${_bash_cmd}"
        COMMENT "clang-tidy (CUDA) ${_src}"
        VERBATIM
      )
    endif ()
  endforeach ()
endfunction ()
