# ==============================================================================
# apex/Upx.cmake - UPX-compressed artifact copies
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

option(UPX_COMPRESS_SHARED "Allow UPX on shared libraries" OFF)

# ------------------------------------------------------------------------------
# apex_add_upx_copy(<target>)
#
# Create UPX-compressed copy under <artifact_dir>/upx/.
# Respects ENABLE_UPX toggle. Only compresses executables by default.
# Only runs for Release/MinSizeRel builds (skips Debug/RelWithDebInfo).
# ------------------------------------------------------------------------------
function (apex_add_upx_copy _target)
  if (NOT ENABLE_UPX)
    return()
  endif ()

  # Skip for Debug builds - no point compressing debug binaries
  if (CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
    if (NOT _build_type_upper MATCHES "^(RELEASE|MINSIZEREL)$")
      return()
    endif ()
  endif ()

  apex_guard(${_target})

  get_target_property(_tgt_type ${_target} TYPE)
  if (NOT _tgt_type OR _tgt_type STREQUAL "STATIC_LIBRARY")
    return()
  endif ()

  set(_do_upx FALSE)
  if (_tgt_type STREQUAL "EXECUTABLE")
    set(_do_upx TRUE)
  elseif ((_tgt_type STREQUAL "SHARED_LIBRARY" OR _tgt_type STREQUAL "MODULE_LIBRARY")
          AND UPX_COMPRESS_SHARED
  )
    set(_do_upx TRUE)
  endif ()

  if (NOT _do_upx)
    return()
  endif ()

  find_program(UPX_EXECUTABLE upx QUIET)
  if (NOT UPX_EXECUTABLE)
    message(WARNING "ENABLE_UPX=ON but upx not found; skipping '${_target}'")
    return()
  endif ()

  set(_upx_dir "$<TARGET_FILE_DIR:${_target}>/upx")
  set(_upx_file "${_upx_dir}/$<TARGET_FILE_NAME:${_target}>.upx")

  add_custom_command(
    TARGET ${_target}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_upx_dir}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${_upx_file}"
    COMMAND
      /usr/bin/env sh -c
      "'${UPX_EXECUTABLE}' -q --best --lzma -o '${_upx_file}' '$<TARGET_FILE:${_target}>' >/dev/null 2>&1 || cp '$<TARGET_FILE:${_target}>' '${_upx_file}'"
    COMMENT "UPX: ${_target}"
    VERBATIM
  )

  set_property(
    TARGET ${_target}
    APPEND
    PROPERTY ADDITIONAL_CLEAN_FILES "${_upx_file}"
  )
endfunction ()
