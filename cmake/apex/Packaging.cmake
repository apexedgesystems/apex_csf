# ==============================================================================
# apex/Packaging.cmake - Application packaging via CMake install components
# ==============================================================================
#
# Each app registered via apex_add_app() gets a deployable bundle derived from
# the build graph: the app binary, its transitive project shared-library closure,
# a bank-aware run.sh launcher, and (if registered) its TPRM config -- staged in
# the on-device ApexFileSystem layout (bank_a/{bin,libs,tprm} + run.sh).
#
# The library closure comes from the CMake link graph, not a post-hoc ELF/readelf
# walk: the libraries are our own targets, so the graph is the authoritative
# closure -- deterministic and cross-architecture-trivial (no readelf needed).
#
# Usage (automatic -- apps registered via apex_add_app() are packaged):
#   ninja package_ApexHilDemo      # cmake --install --component + tarball
#   make package APP=ApexHilDemo
#   make release APP=ApexHilDemo
#
# Architecture:
#   1. apex_add_app() calls _apex_register_packagable_app() to track the name
#   2. apex_set_app_tprm() optionally registers a TPRM archive path
#   3. apex_finalize_packages() (root, after all targets exist) emits the
#      per-app install(COMPONENT) rules and a thin package_<APP> target that runs
#      cmake --install + tar.
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# _apex_register_packagable_app(app_name)
#
# Track an app for deferred package rule creation. Called by apex_add_app().
# ------------------------------------------------------------------------------
function (_apex_register_packagable_app app_name)
  set_property(GLOBAL APPEND PROPERTY APEX_PACKAGABLE_APPS "${app_name}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_set_app_tprm(app_name tprm_path)
#
# Register a TPRM master archive for an app, included as bank_a/tprm/master.tprm.
# ------------------------------------------------------------------------------
function (apex_set_app_tprm app_name tprm_path)
  set_property(GLOBAL PROPERTY APEX_APP_TPRM_${app_name} "${tprm_path}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_set_app_extra_bins(app_name bin...)
#
# Register additional executables to ship in an app's bundle (e.g. the watchdog
# supervisor). Their own shared-lib closures are unioned into bank_a/libs.
# ------------------------------------------------------------------------------
function (apex_set_app_extra_bins app_name)
  set_property(GLOBAL PROPERTY APEX_APP_EXTRA_BINS_${app_name} "${ARGN}")
endfunction ()

# ------------------------------------------------------------------------------
# _apex_collect_shared_deps(<root-target> <out-var>)
#
# BFS the transitive link closure of <root-target>, collecting built (non
# imported) SHARED_LIBRARY targets -- the project libraries that must ship beside
# the binary. Aliases are resolved to their real target names. System and
# imported libraries are skipped (the target already loads those from the OS).
# ------------------------------------------------------------------------------
function (_apex_collect_shared_deps _root _out)
  set(_seen "")
  set(_queue "${_root}")
  set(_libs "")
  while (_queue)
    list(POP_FRONT _queue _t)
    if (NOT TARGET "${_t}")
      continue()
    endif ()
    get_target_property(_alias "${_t}" ALIASED_TARGET)
    if (_alias)
      set(_t "${_alias}")
    endif ()
    if ("${_t}" IN_LIST _seen)
      continue()
    endif ()
    list(APPEND _seen "${_t}")

    get_target_property(_type "${_t}" TYPE)
    get_target_property(_imported "${_t}" IMPORTED)
    if (_type STREQUAL "SHARED_LIBRARY"
        AND NOT _imported
        AND NOT "${_t}" STREQUAL "${_root}"
    )
      list(APPEND _libs "${_t}")
    endif ()

    foreach (_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_deps "${_t}" ${_prop})
      if (_deps)
        foreach (_d IN LISTS _deps)
          if (TARGET "${_d}")
            list(APPEND _queue "${_d}")
          endif ()
        endforeach ()
      endif ()
    endforeach ()
  endwhile ()
  list(REMOVE_DUPLICATES _libs)
  set(${_out}
      "${_libs}"
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_finalize_packages()
#
# Emit install(COMPONENT) rules + a package_<APP> target for every registered
# app. Call once at the end of the root CMakeLists.txt (after all targets and
# their links exist, so the closure walk sees the full graph).
#
# Per app, component <APP> installs into the deploy prefix:
#   bank_a/bin/<APP>             the binary
#   bank_a/libs/*.so*            the project shared-lib closure (runtime chain)
#   bank_a/tprm/master.tprm      TPRM config (if registered)
#   run.sh                       bank-aware launcher
#
# package_<APP> runs `cmake --install --component <APP>` into packages/<APP> and
# tars it to packages/<APP>.tar.gz.
# ------------------------------------------------------------------------------
function (apex_finalize_packages)
  get_property(_apps GLOBAL PROPERTY APEX_PACKAGABLE_APPS)
  if (NOT _apps)
    return()
  endif ()

  set(_launcher "${CMAKE_SOURCE_DIR}/cmake/apex/assets/run.sh")

  foreach (_app IN LISTS _apps)
    # Skip if the app target wasn't created (e.g., requirements not met)
    if (NOT TARGET ${_app})
      continue()
    endif ()

    # Binaries (the app + any registered extra bins, e.g. the watchdog) into
    # bank_a/bin; the union of their project shared-lib closures into bank_a/libs.
    # NAMELINK_SKIP ships the runtime chain (lib.so.N -> lib.so.N.M.P), not the
    # dev namelink (lib.so).
    set(_bins ${_app})
    get_property(_extra GLOBAL PROPERTY APEX_APP_EXTRA_BINS_${_app})
    if (_extra)
      list(APPEND _bins ${_extra})
    endif ()

    set(_libs "")
    foreach (_bin IN LISTS _bins)
      if (NOT TARGET ${_bin})
        message(WARNING "[apex] package ${_app}: extra bin '${_bin}' is not a target; skipping")
        continue()
      endif ()
      install(TARGETS ${_bin} RUNTIME DESTINATION bank_a/bin COMPONENT ${_app})
      _apex_collect_shared_deps(${_bin} _binlibs)
      list(APPEND _libs ${_binlibs})
    endforeach ()

    list(REMOVE_DUPLICATES _libs)
    if (_libs)
      install(
        TARGETS ${_libs}
        LIBRARY DESTINATION bank_a/libs
                NAMELINK_SKIP
                COMPONENT ${_app}
      )
    endif ()
    install(
      PROGRAMS "${_launcher}"
      DESTINATION .
      COMPONENT ${_app}
    )

    get_property(_tprm GLOBAL PROPERTY APEX_APP_TPRM_${_app})
    if (_tprm)
      install(
        FILES "${CMAKE_SOURCE_DIR}/${_tprm}"
        DESTINATION bank_a/tprm
        RENAME master.tprm
        COMPONENT ${_app}
      )
    endif ()

    # Thin packaging target: stage the component, then tar it.
    set(_pkgroot "${CMAKE_BINARY_DIR}/packages")
    add_custom_target(
      package_${_app}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_pkgroot}"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_pkgroot}/${_app}"
      COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --component ${_app} --prefix
              "${_pkgroot}/${_app}"
      COMMAND ${CMAKE_COMMAND} -E chdir "${_pkgroot}" ${CMAKE_COMMAND} -E tar czf "${_app}.tar.gz"
              "${_app}"
      DEPENDS ${_app}
      COMMENT "[package] ${_app} -> cmake --install (bank_a + run.sh)"
      VERBATIM
    )

    if (APEX_TARGETS_VERBOSE)
      list(LENGTH _libs _n)
      message(STATUS "[apex] PACKAGE package_${_app} (graph closure: ${_n} libs)")
    endif ()
  endforeach ()

  list(LENGTH _apps _count)
  message(STATUS "[apex] Registered ${_count} package target(s)")
endfunction ()
