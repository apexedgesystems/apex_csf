# ==============================================================================
# apex/Manifest.cmake - per-library support contracts (lib.manifest)
#
# A library may ship a `lib.manifest` beside its CMakeLists.txt: an optional,
# machine-read support contract that says which platforms and hosted dialects
# the library is built and verified for. No manifest, no change in behavior —
# adoption is voluntary and per lib.
#
# Format (flat KEY value lines, `#` comments):
#
#   name      utilities_math_vecmat      # must match the CMake target
#   posix_cpp 17 20 23                   # hosted dialects the lib supports
#   mcu       stm32 avr pico esp32       # APEX_HAL_PLATFORM names
#   probe     probe/Probe.cpp            # verification TU, relative to lib dir
#
# The manifest names WHAT the library supports; toolchain files keep owning
# HOW each platform compiles (dialect, flags, sysroot). When a probe is
# declared it compiles as an OBJECT target with -Werror:
#   - hosted builds: once per declared posix_cpp dialect — hosted CI builds
#     at C++23, so each lower dialect is a claim needing its own compile.
#     Libraries claiming MCU platforms also compile their hosted probes with
#     -fno-exceptions -fno-rtti -fno-threadsafe-statics, mirroring the
#     discipline every MCU toolchain enforces.
#   - bare-metal builds: one object under the real cross toolchain at the
#     toolchain's own dialect, when APEX_HAL_PLATFORM is in the mcu list.
#     This is what verifies freestanding truth for surfaces no firmware
#     consumes yet; the toolchain is the instrument that can call a
#     manifest's claim false.
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# _apex_apply_manifest(<target>)
#
# Called by apex_add_library / apex_add_interface_library after the target
# exists. Reads ${CMAKE_CURRENT_SOURCE_DIR}/lib.manifest if present and wires
# the declared probe targets. A `name` key that disagrees with the CMake
# target is a hard error — the manifest travels with the directory, and
# silent drift would falsify the contract.
# ------------------------------------------------------------------------------
function (_apex_apply_manifest _target)
  set(_mf "${CMAKE_CURRENT_SOURCE_DIR}/lib.manifest")
  if (NOT EXISTS "${_mf}")
    return()
  endif ()

  set(_name "")
  set(_posix "")
  set(_mcu "")
  set(_probe "")
  file(STRINGS "${_mf}" _lines)
  foreach (_line IN LISTS _lines)
    string(STRIP "${_line}" _line)
    if (_line STREQUAL "" OR _line MATCHES "^#")
      continue()
    endif ()
    string(REGEX REPLACE "[ \t]+" ";" _tok "${_line}")
    list(POP_FRONT _tok _key)
    if (_key STREQUAL "name")
      list(GET _tok 0 _name)
    elseif (_key STREQUAL "posix_cpp")
      set(_posix ${_tok})
    elseif (_key STREQUAL "mcu")
      set(_mcu ${_tok})
    elseif (_key STREQUAL "probe")
      list(GET _tok 0 _probe)
    else ()
      message(FATAL_ERROR "[apex] ${_mf}: unknown manifest key '${_key}'")
    endif ()
  endforeach ()

  if (_name AND NOT _name STREQUAL "${_target}")
    message(FATAL_ERROR "[apex] ${_mf}: name '${_name}' != target '${_target}'")
  endif ()

  if (NOT _probe)
    return()
  endif ()
  set(_probe_src "${CMAKE_CURRENT_SOURCE_DIR}/${_probe}")
  if (NOT EXISTS "${_probe_src}")
    message(FATAL_ERROR "[apex] ${_mf}: probe '${_probe}' not found")
  endif ()

  # Probes include repo-root-relative headers only; no target deps, so a
  # probe can never silently satisfy a claim through a hosted-only usage
  # requirement.
  if (APEX_PLATFORM_BAREMETAL)
    if (NOT APEX_HAL_PLATFORM IN_LIST _mcu)
      return()
    endif ()
    set(_tgt ${_target}_probe)
    add_library(${_tgt} OBJECT "${_probe_src}")
    target_include_directories(${_tgt} PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(${_tgt} PRIVATE -Werror)
    return()
  endif ()

  if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    return()
  endif ()
  foreach (_std IN LISTS _posix)
    set(_tgt ${_target}_probe_cpp${_std})
    add_library(${_tgt} OBJECT "${_probe_src}")
    set_target_properties(
      ${_tgt}
      PROPERTIES CXX_STANDARD ${_std}
                 CXX_STANDARD_REQUIRED ON
                 CXX_EXTENSIONS OFF
    )
    target_include_directories(${_tgt} PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(${_tgt} PRIVATE -Werror)
    if (_mcu)
      target_compile_options(${_tgt} PRIVATE -fno-exceptions -fno-rtti -fno-threadsafe-statics)
    endif ()
  endforeach ()
endfunction ()
