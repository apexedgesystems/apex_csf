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
#     EXEC ApexHilDemo                          # exactly one executive
#     TPRM ApexHilDemo/master_1khz.tprm         # optional; product ref or repo path
#     TPRM_FALLBACK ApexHilDemo/safe_master.tprm # optional; pre-stages bank_b
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
#     TOOLS       my_cli_tool tprm_template    # tools of any kind, by name
#     FILES       docs/OPERATIONS.md           # arbitrary support files/dirs
#   )
#   ninja package_GroundStation
#
# A package is the user's filesystem for one target: it is copied onto the
# target and run in place, with nothing installed, built or fetched afterwards.
# Whatever cannot work that way fails the build: a tool name nothing provides,
# a tool built for another architecture, a tool in a bare-metal build (firmware
# ships as an image and has no filesystem to carry one).
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
# subdir. FILES are arbitrary files/dirs (docs, configs) staged at the bundle
# root. TOOLS are tools by name, staged under tools/ so each runs from wherever
# the package is copied:
#
#   a C++ executable target      tools/bin/<name>, its project shared libraries
#                                in tools/libs (found through $ORIGIN/../libs)
#   a registered tool            see apex_register_tool: a prebuilt executable
#                                or a shell script to tools/bin/<name>; a Python
#                                console script as a launcher in tools/bin with
#                                its distribution and what that requires in
#                                tools/lib/python (the target provides python3)
#
# tools/REQUIREMENTS.txt lists what each staged tool needs on the target. A name
# that is neither is a configure error, and so is TOOLS in a bare-metal build.
# ------------------------------------------------------------------------------
function (apex_add_bundle)
  cmake_parse_arguments(B "" "NAME" "DEPLOYMENTS;TOOLS;FILES" ${ARGN})
  apex_require(B_NAME B_DEPLOYMENTS)
  if (APEX_PLATFORM_BAREMETAL AND B_TOOLS)
    message(
      FATAL_ERROR
        "apex_add_bundle(${B_NAME}): TOOLS (${B_TOOLS}) in a bare-metal build. "
        "Firmware ships as a flashable image and has no filesystem to carry tools; "
        "declare the bundle for a hosted target only."
    )
  endif ()
  set_property(GLOBAL APPEND PROPERTY APEX_BUNDLES "${B_NAME}")
  set_property(GLOBAL PROPERTY APEX_BUNDLE_${B_NAME}_DEPLOYMENTS "${B_DEPLOYMENTS}")
  set_property(GLOBAL PROPERTY APEX_BUNDLE_${B_NAME}_TOOLS "${B_TOOLS}")
  set_property(GLOBAL PROPERTY APEX_BUNDLE_${B_NAME}_FILES "${B_FILES}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_register_tool(NAME <name> KIND <bin|script|py> FILE <path> [DEPENDS <target>]
#                    [PYTHON_LIB <dir>])
#
# Make a tool that is not a CMake executable target available to a bundle's
# TOOLS by name. Whatever builds the tool registers it: FILE is where the build
# puts it, DEPENDS the target that produces it.
#
#   bin     a prebuilt executable (a Rust tool)
#   script  a shell script
#   py      a Python console script; PYTHON_LIB is the directory its
#           distribution and that distribution's requirements are installed in
#
# A name can be registered once.
# ------------------------------------------------------------------------------
function (apex_register_tool)
  cmake_parse_arguments(T "" "NAME;KIND;FILE;DEPENDS;PYTHON_LIB" "" ${ARGN})
  apex_require(T_NAME T_KIND T_FILE)
  if (NOT T_KIND MATCHES "^(bin|script|py)$")
    message(FATAL_ERROR "apex_register_tool(${T_NAME}): KIND is bin, script or py, not ${T_KIND}")
  endif ()
  if (T_KIND STREQUAL "py" AND NOT T_PYTHON_LIB)
    message(FATAL_ERROR "apex_register_tool(${T_NAME}): a py tool names its PYTHON_LIB")
  endif ()
  get_property(_known GLOBAL PROPERTY APEX_TOOL_${T_NAME}_KIND)
  if (_known)
    get_property(_known_file GLOBAL PROPERTY APEX_TOOL_${T_NAME}_FILE)
    message(
      FATAL_ERROR
        "apex_register_tool(${T_NAME}): already registered as a ${_known} tool at ${_known_file}"
    )
  endif ()
  set_property(GLOBAL PROPERTY APEX_TOOL_${T_NAME}_KIND "${T_KIND}")
  set_property(GLOBAL PROPERTY APEX_TOOL_${T_NAME}_FILE "${T_FILE}")
  set_property(GLOBAL PROPERTY APEX_TOOL_${T_NAME}_DEPENDS "${T_DEPENDS}")
  set_property(GLOBAL PROPERTY APEX_TOOL_${T_NAME}_PYTHON_LIB "${T_PYTHON_LIB}")
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

  # The module carries its assets beside itself.
  set(_launcher "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets/run.sh")
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
    # A bundle stages this deployment through the same install component, so
    # it needs the same products built first.
    set(_pkgdeps_${_name} ${_exec} ${_tprm_deps})
    math(EXPR _count "${_count} + 1")
  endforeach ()

  # --------------------------------------------------------------------------
  # Bundles: consolidate several deployments + tools + support files into one
  # tarball. Each deployment is staged into its own subdir (reusing its
  # component); tools go under tools/; files land at the root.
  # --------------------------------------------------------------------------
  set(_stage_tools "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets/stage_bundle_tools.py")
  find_program(_APEX_PYTHON3 python3)
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

    # The package's architecture is its executive's. A bundle none of whose
    # executives exist on this platform has nothing to ship.
    set(_bref "")
    foreach (_d IN LISTS _bdeps)
      get_property(_dexec GLOBAL PROPERTY APEX_DEPLOY_${_d}_EXEC)
      if (NOT _bref
          AND _dexec
          AND TARGET ${_dexec}
      )
        set(_bref "${_dexec}")
      endif ()
    endforeach ()
    if (NOT _bref)
      message(STATUS "[apex] Bundle ${_bname} skipped: none of its executives build here")
      continue()
    endif ()

    # Tools: a C++ target installs with its shared-lib closure; a registered
    # tool is staged by the stage script; any other name is refused.
    set(_toollibs "")
    set(_stage_args "")
    foreach (_t IN LISTS _btools)
      get_property(_tkind GLOBAL PROPERTY APEX_TOOL_${_t}_KIND)
      if (TARGET ${_t})
        get_target_property(_ttype ${_t} TYPE)
        if (NOT _ttype STREQUAL "EXECUTABLE")
          message(
            FATAL_ERROR
              "apex_add_bundle(${_bname}): TOOLS names ${_t}, a ${_ttype} target. "
              "A tool is an executable target or a tool registered with apex_register_tool."
          )
        endif ()
        install(TARGETS ${_t} RUNTIME DESTINATION tools/bin COMPONENT ${_comp})
        _apex_collect_shared_deps(${_t} _tl)
        list(APPEND _toollibs ${_tl})
        list(APPEND _bdepends ${_t})
        set(_has_extra TRUE)
      elseif (_tkind)
        get_property(_tfile GLOBAL PROPERTY APEX_TOOL_${_t}_FILE)
        get_property(_tdep GLOBAL PROPERTY APEX_TOOL_${_t}_DEPENDS)
        get_property(_tpylib GLOBAL PROPERTY APEX_TOOL_${_t}_PYTHON_LIB)
        if (_tkind STREQUAL "py")
          list(APPEND _stage_args --tool "py:${_t}:${_tfile}:${_tpylib}")
        else ()
          list(APPEND _stage_args --tool "${_tkind}:${_t}:${_tfile}")
        endif ()
        if (_tdep)
          list(APPEND _bdepends ${_tdep})
        endif ()
      else ()
        message(
          FATAL_ERROR
            "apex_add_bundle(${_bname}): TOOLS names ${_t}, which nothing provides. "
            "A tool is an executable target or a tool registered with apex_register_tool "
            "(the tools build registers its Rust, shell and Python tools when it is enabled "
            "and their toolchains are present)."
        )
      endif ()
    endforeach ()
    if (_btools AND NOT _APEX_PYTHON3)
      message(
        FATAL_ERROR "apex_add_bundle(${_bname}): staging TOOLS needs python3 on the build machine"
      )
    endif ()
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
      list(APPEND _bdepends ${_pkgdeps_${_d}})
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
    # Every staged tool is checked against the executive's architecture, the
    # C++ ones included; a mismatch fails the package step.
    if (_btools)
      list(
        APPEND
        _subinstalls
        COMMAND
        "${_APEX_PYTHON3}"
        "${_stage_tools}"
        --bundle
        ${_bname}
        --dest
        "${_pkgroot}/${_bname}/tools"
        --reference
        "$<TARGET_FILE:${_bref}>"
        ${_stage_args}
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
