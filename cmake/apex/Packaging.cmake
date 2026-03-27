# ==============================================================================
# apex/Packaging.cmake - Application packaging target registration
# ==============================================================================
#
# Creates package_<APP> custom targets for each app registered via
# apex_add_app(). Each target invokes pkg_resolve.sh to resolve ELF
# dependencies, stage the binary and its shared libraries, include TPRM
# configuration, generate a launch script, and create a deployable tarball.
#
# Usage (automatic -- apps registered via apex_add_app() are packaged):
#   # After building:
#   ninja package_ApexHilDemo
#   ninja package_ApexDemo
#
# Or via Make:
#   make package APP=ApexHilDemo
#   make release APP=ApexHilDemo
#
# Architecture:
#   1. apex_add_app() calls _apex_register_packagable_app() to track the name
#   2. apex_set_app_tprm() optionally registers a TPRM archive path
#   3. apex_finalize_packages() creates package_<APP> targets that invoke
#      tools/sh/bin/pkg_resolve.sh for dependency resolution and staging
#
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# _apex_register_packagable_app(app_name)
#
# Track an app for deferred package target creation.
# Called automatically by apex_add_app().
# ------------------------------------------------------------------------------
function (_apex_register_packagable_app app_name)
  set_property(GLOBAL APPEND PROPERTY APEX_PACKAGABLE_APPS "${app_name}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_set_app_tprm(app_name tprm_path)
#
# Register a TPRM master archive for an app. The TPRM file will be included
# in the package output under tprm/master.tprm.
# ------------------------------------------------------------------------------
function (apex_set_app_tprm app_name tprm_path)
  set_property(GLOBAL PROPERTY APEX_APP_TPRM_${app_name} "${tprm_path}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_finalize_packages()
#
# Create package_<APP> custom targets for all registered apps.
# Call this once at the end of root CMakeLists.txt.
#
# Each target:
#   - Depends on the app binary (rebuilds if source changes)
#   - Runs pkg_resolve.sh to resolve deps, stage files, and create tarball
#
# Output:
#   ${CMAKE_BINARY_DIR}/packages/<APP>/bin/<APP>
#   ${CMAKE_BINARY_DIR}/packages/<APP>/lib/*.so
#   ${CMAKE_BINARY_DIR}/packages/<APP>/tprm/master.tprm  (if TPRM registered)
#   ${CMAKE_BINARY_DIR}/packages/<APP>/run.sh
#   ${CMAKE_BINARY_DIR}/packages/<APP>.tar.gz
# ------------------------------------------------------------------------------
function (apex_finalize_packages)
  get_property(_apps GLOBAL PROPERTY APEX_PACKAGABLE_APPS)

  if (NOT _apps)
    return()
  endif ()

  set(_pkg_script "${CMAKE_SOURCE_DIR}/tools/sh/bin/pkg_resolve.sh")

  foreach (_app IN LISTS _apps)
    # Skip if the app target wasn't actually created (e.g., requirements not met)
    if (NOT TARGET ${_app})
      continue()
    endif ()

    # Build the command
    set(_cmd bash "${_pkg_script}" --app "${_app}" --build-dir "${CMAKE_BINARY_DIR}")

    # Include TPRM if registered
    get_property(_tprm GLOBAL PROPERTY APEX_APP_TPRM_${_app})
    if (_tprm)
      list(APPEND _cmd --tprm "${CMAKE_SOURCE_DIR}/${_tprm}")
    endif ()

    add_custom_target(
      package_${_app}
      COMMAND ${_cmd}
      DEPENDS ${_app}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "[package] Creating deployable package for ${_app}"
      VERBATIM
    )

    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] PACKAGE package_${_app} -> pkg_resolve.sh")
    endif ()
  endforeach ()

  list(LENGTH _apps _count)
  message(STATUS "[apex] Registered ${_count} package target(s)")
endfunction ()
