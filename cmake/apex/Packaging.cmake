# ==============================================================================
# apex/Packaging.cmake - Deployable packaging via CMake install components
# ==============================================================================
#
# A *deployment* is one apex filesystem: bank_a/{bin,libs,tprm} + a generic
# run.sh launcher, owned by exactly ONE executive. One filesystem = one
# --fs-root owner is the safety invariant: two executives sharing a filesystem
# would collide on system.log, the banks, and telemetry, so the model does not
# let you express that. A supervisor (the watchdog) is its own single-exec
# deployment that launches another across filesystems -- it owns no filesystem.
# apex_finalize_packages() emits the per-deployment install(COMPONENT) rules and
# a package_<NAME> target (cmake --install + tar).
#
# The shared-library closure is derived from the build graph (the executive's
# transitive link targets), not a readelf walk -- the libraries are our own
# targets, so the graph is the authoritative closure: deterministic and
# cross-architecture-trivial (no readelf, no resolution hints). Deployed binaries
# carry a $ORIGIN/../libs RPATH, so each deployment is self-contained.
#
# Usage:
#   apex_add_deployment(
#     NAME ApexHilDemo
#     EXEC ApexHilDemo                                 # exactly one executive
#     TPRM demos/apex_hil_demo/tprm/master_1khz.tprm    # optional
#   )
#   ninja package_ApexHilDemo        # cmake --install --component + tarball
#   make package APP=ApexHilDemo
#   make release APP=ApexHilDemo
#
# apex_add_bundle() is the level above: it consolidates several deployments +
# custom tools + arbitrary support files (docs, configs) into one shippable
# tarball -- for a system that ships more than one apex filesystem at once.
#
#   apex_add_bundle(
#     NAME        GroundStation
#     DEPLOYMENTS ApexOpsDemo ApexActionDemo   # each its own apex filesystem
#     TOOLS       my_cli_tool                  # extra executables (+ their libs)
#     FILES       docs/OPERATIONS.md           # arbitrary support files/dirs
#   )
#   ninja package_GroundStation
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# apex_add_deployment(NAME <name> EXEC <exec> [TPRM <ref>] [TPRM_FALLBACK <ref>])
#
# Declare a deployable apex filesystem. NAME is the bundle/dir (and the
# package_<NAME> target). EXEC is the single executive target that owns the
# filesystem. TPRM is the optional master config staged as
# bank_a/tprm/master.tprm (what boots). TPRM_FALLBACK stages a second master
# as bank_b/tprm/master.tprm -- a pre-staged known-good configuration the
# operator can swap to with zero uplink (RELOAD_TPRM reads the inactive
# bank).
#
# A <ref> is either an apex_add_tprm product ("<App>/<master.tprm>",
# generated at build time) or a repo-relative file path.
# ------------------------------------------------------------------------------
function (apex_add_deployment)
  cmake_parse_arguments(D "" "NAME;EXEC;TPRM;TPRM_FALLBACK" "" ${ARGN})
  apex_require(D_NAME D_EXEC)
  set_property(GLOBAL APPEND PROPERTY APEX_DEPLOYMENTS "${D_NAME}")
  set_property(GLOBAL PROPERTY APEX_DEPLOY_${D_NAME}_EXEC "${D_EXEC}")
  set_property(GLOBAL PROPERTY APEX_DEPLOY_${D_NAME}_TPRM "${D_TPRM}")
  set_property(GLOBAL PROPERTY APEX_DEPLOY_${D_NAME}_TPRM_FALLBACK "${D_TPRM_FALLBACK}")
endfunction ()

# Resolve a TPRM <ref> to (path, dependency target). Generated products win;
# anything else is a repo-relative source path with no extra dependency.
function (_apex_resolve_tprm _ref _out_path _out_dep)
  get_property(_gen GLOBAL PROPERTY APEX_TPRM_PRODUCT_${_ref})
  if (_gen)
    string(REGEX REPLACE "/.*$" "" _app "${_ref}")
    get_property(_dep GLOBAL PROPERTY APEX_TPRM_TARGET_${_app})
    set(${_out_path}
        "${_gen}"
        PARENT_SCOPE
    )
    set(${_out_dep}
        "${_dep}"
        PARENT_SCOPE
    )
  else ()
    set(${_out_path}
        "${CMAKE_SOURCE_DIR}/${_ref}"
        PARENT_SCOPE
    )
    set(${_out_dep}
        ""
        PARENT_SCOPE
    )
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_bundle(NAME <name> DEPLOYMENTS <dep>... [TOOLS <target>...] [FILES <path>...])
#
# Declare a shippable artifact that consolidates several deployments plus custom
# tools and support files. NAME is the bundle/dir (and the package_<NAME>
# target). DEPLOYMENTS are apex_add_deployment() names, each staged into its own
# subdir. TOOLS are extra executable targets (staged with their shared-lib
# closures under tools/). FILES are arbitrary files/dirs (docs, configs) staged
# at the bundle root.
# ------------------------------------------------------------------------------
function (apex_add_bundle)
  cmake_parse_arguments(B "" "NAME" "DEPLOYMENTS;TOOLS;FILES" ${ARGN})
  apex_require(B_NAME B_DEPLOYMENTS)
  set_property(GLOBAL APPEND PROPERTY APEX_BUNDLES "${B_NAME}")
  set_property(GLOBAL PROPERTY APEX_BUNDLE_${B_NAME}_DEPLOYMENTS "${B_DEPLOYMENTS}")
  set_property(GLOBAL PROPERTY APEX_BUNDLE_${B_NAME}_TOOLS "${B_TOOLS}")
  set_property(GLOBAL PROPERTY APEX_BUNDLE_${B_NAME}_FILES "${B_FILES}")
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
# Emit install(COMPONENT) rules + a package_<NAME> target for every declared
# deployment. Call once at the end of the root CMakeLists.txt (after all targets
# and their links exist, so the closure walk sees the full graph).
#
# Per deployment, component <NAME> installs into the deploy prefix:
#   bank_a/bin/<exec>            the single executive
#   bank_a/libs/*.so*            its project shared-lib closure
#   bank_a/tprm/master.tprm      TPRM config (if declared)
#   run.sh                       the generic launcher
#
# package_<NAME> runs `cmake --install --component <NAME>` into packages/<NAME>
# and tars it to packages/<NAME>.tar.gz.
# ------------------------------------------------------------------------------
function (apex_finalize_packages)
  get_property(_deps GLOBAL PROPERTY APEX_DEPLOYMENTS)
  if (NOT _deps)
    return()
  endif ()

  set(_launcher "${CMAKE_SOURCE_DIR}/cmake/apex/assets/run.sh")
  set(_count 0)

  foreach (_name IN LISTS _deps)
    get_property(_exec GLOBAL PROPERTY APEX_DEPLOY_${_name}_EXEC)
    get_property(_tprm GLOBAL PROPERTY APEX_DEPLOY_${_name}_TPRM)

    # Skip if the executive target does not exist (requirements may skip platforms).
    if (NOT TARGET ${_exec})
      continue()
    endif ()

    # The executive into bank_a/bin; its shared-lib closure into bank_a/libs.
    # NAMELINK_SKIP ships the runtime SOVERSION chain, not the dev namelink.
    install(TARGETS ${_exec} RUNTIME DESTINATION bank_a/bin COMPONENT ${_name})
    _apex_collect_shared_deps(${_exec} _libs)
    list(REMOVE_DUPLICATES _libs)
    if (_libs)
      install(
        TARGETS ${_libs}
        LIBRARY DESTINATION bank_a/libs
                NAMELINK_SKIP
                COMPONENT ${_name}
      )
    endif ()

    install(
      PROGRAMS "${_launcher}"
      DESTINATION .
      COMPONENT ${_name}
    )

    set(_tprm_deps "")
    if (_tprm)
      _apex_resolve_tprm("${_tprm}" _tprm_path _tprm_dep)
      install(
        FILES "${_tprm_path}"
        DESTINATION bank_a/tprm
        RENAME master.tprm
        COMPONENT ${_name}
      )
      list(APPEND _tprm_deps ${_tprm_dep})

      # A product-referenced TPRM carries its app's mission bank: the
      # generated rts/ats sequences stage into bank_a so the deployed
      # filesystem boots with the manifest's full sequence set. bank_b
      # stays minimal -- fallback master only.
      get_property(_tprm_is_product GLOBAL PROPERTY APEX_TPRM_PRODUCT_${_tprm})
      if (_tprm_is_product)
        string(REGEX REPLACE "/.*$" "" _tprm_app "${_tprm}")
        get_property(_bank_rts GLOBAL PROPERTY APEX_TPRM_BANK_RTS_${_tprm_app})
        get_property(_bank_ats GLOBAL PROPERTY APEX_TPRM_BANK_ATS_${_tprm_app})
        if (_bank_rts)
          install(
            FILES ${_bank_rts}
            DESTINATION bank_a/rts
            COMPONENT ${_name}
          )
        endif ()
        if (_bank_ats)
          install(
            FILES ${_bank_ats}
            DESTINATION bank_a/ats
            COMPONENT ${_name}
          )
        endif ()
      endif ()
    endif ()

    # The fallback master pre-stages bank_b so a swap to the known-good
    # configuration costs one command and zero uplink bytes.
    get_property(_tprm_fb GLOBAL PROPERTY APEX_DEPLOY_${_name}_TPRM_FALLBACK)
    if (_tprm_fb)
      _apex_resolve_tprm("${_tprm_fb}" _fb_path _fb_dep)
      install(
        FILES "${_fb_path}"
        DESTINATION bank_b/tprm
        RENAME master.tprm
        COMPONENT ${_name}
      )
      list(APPEND _tprm_deps ${_fb_dep})
    endif ()

    set(_pkgroot "${CMAKE_BINARY_DIR}/packages")
    add_custom_target(
      package_${_name}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_pkgroot}"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_pkgroot}/${_name}"
      COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --component ${_name} --prefix
              "${_pkgroot}/${_name}"
      COMMAND ${CMAKE_COMMAND} -E chdir "${_pkgroot}" ${CMAKE_COMMAND} -E tar czf "${_name}.tar.gz"
              "${_name}"
      DEPENDS ${_exec} ${_tprm_deps}
      COMMENT "[package] ${_name} -> cmake --install (bank_a + run.sh)"
      VERBATIM
    )
    math(EXPR _count "${_count} + 1")
  endforeach ()

  # --------------------------------------------------------------------------
  # Bundles: consolidate several deployments + tools + support files into one
  # tarball. Each deployment is staged into its own subdir (reusing its
  # component); tools go under tools/ with their closures; files land at the root.
  # --------------------------------------------------------------------------
  get_property(_bundles GLOBAL PROPERTY APEX_BUNDLES)
  set(_bcount 0)
  set(_pkgroot "${CMAKE_BINARY_DIR}/packages")

  foreach (_bname IN LISTS _bundles)
    get_property(_bdeps GLOBAL PROPERTY APEX_BUNDLE_${_bname}_DEPLOYMENTS)
    get_property(_btools GLOBAL PROPERTY APEX_BUNDLE_${_bname}_TOOLS)
    get_property(_bfiles GLOBAL PROPERTY APEX_BUNDLE_${_bname}_FILES)
    set(_comp "bundle_${_bname}")
    set(_has_extra FALSE)
    set(_bdepends "")

    # Tools: binary + shared-lib closure under tools/.
    set(_toollibs "")
    foreach (_t IN LISTS _btools)
      if (TARGET ${_t})
        install(TARGETS ${_t} RUNTIME DESTINATION tools/bin COMPONENT ${_comp})
        _apex_collect_shared_deps(${_t} _tl)
        list(APPEND _toollibs ${_tl})
        list(APPEND _bdepends ${_t})
        set(_has_extra TRUE)
      endif ()
    endforeach ()
    list(REMOVE_DUPLICATES _toollibs)
    if (_toollibs)
      install(
        TARGETS ${_toollibs}
        LIBRARY DESTINATION tools/libs
                NAMELINK_SKIP
                COMPONENT ${_comp}
      )
    endif ()

    # Support files/dirs (docs, configs) at the bundle root.
    foreach (_f IN LISTS _bfiles)
      if (IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${_f}")
        install(
          DIRECTORY "${CMAKE_SOURCE_DIR}/${_f}"
          DESTINATION .
          COMPONENT ${_comp}
        )
      else ()
        install(
          FILES "${CMAKE_SOURCE_DIR}/${_f}"
          DESTINATION .
          COMPONENT ${_comp}
        )
      endif ()
      set(_has_extra TRUE)
    endforeach ()

    # Stage each deployment into its own subdir (reuse its per-deployment
    # component), then the bundle's own tools/files component at the root.
    set(_subinstalls "")
    foreach (_d IN LISTS _bdeps)
      list(
        APPEND
        _subinstalls
        COMMAND
        ${CMAKE_COMMAND}
        --install
        "${CMAKE_BINARY_DIR}"
        --component
        ${_d}
        --prefix
        "${_pkgroot}/${_bname}/${_d}"
      )
      get_property(_dexec GLOBAL PROPERTY APEX_DEPLOY_${_d}_EXEC)
      if (TARGET ${_dexec})
        list(APPEND _bdepends ${_dexec})
      endif ()
    endforeach ()
    if (_has_extra)
      list(
        APPEND
        _subinstalls
        COMMAND
        ${CMAKE_COMMAND}
        --install
        "${CMAKE_BINARY_DIR}"
        --component
        ${_comp}
        --prefix
        "${_pkgroot}/${_bname}"
      )
    endif ()

    add_custom_target(
      package_${_bname}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_pkgroot}"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_pkgroot}/${_bname}" ${_subinstalls}
      COMMAND ${CMAKE_COMMAND} -E chdir "${_pkgroot}" ${CMAKE_COMMAND} -E tar czf "${_bname}.tar.gz"
              "${_bname}"
      DEPENDS ${_bdepends}
      COMMENT "[bundle] ${_bname} -> ${_bdeps}"
      VERBATIM
    )
    math(EXPR _bcount "${_bcount} + 1")
  endforeach ()

  message(STATUS "[apex] Registered ${_count} deployment + ${_bcount} bundle package target(s)")
endfunction ()
