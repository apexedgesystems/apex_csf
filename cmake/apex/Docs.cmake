# ==============================================================================
# apex/Docs.cmake - Doxygen documentation generation (per-lib HTML + landing page)
# ==============================================================================

include_guard(GLOBAL)

# Global docs target (individual *_docs targets register as dependencies)
if (PROJECT_BUILD_DOCS AND NOT TARGET docs)
  add_custom_target(docs COMMENT "Building all documentation")
endif ()

# ------------------------------------------------------------------------------
# doxygen-awesome-css theme (jothepro/doxygen-awesome-css)
# ------------------------------------------------------------------------------
# Lazy fetch: only fires when an apex_add_doxygen call needs the theme path.
function (_apex_fetch_doxygen_awesome)
  if (DEFINED DOXYGEN_AWESOME_CSS_DIR)
    return()
  endif ()
  include(FetchContent)
  fetchcontent_declare(
    doxygen_awesome_css
    GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
    GIT_TAG v2.3.4
    GIT_SHALLOW TRUE
  )
  fetchcontent_makeavailable(doxygen_awesome_css)
  set(DOXYGEN_AWESOME_CSS_DIR
      "${doxygen_awesome_css_SOURCE_DIR}"
      CACHE INTERNAL "Path to doxygen-awesome-css source"
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_doxygen(<target>
#                  [README <path>] [SRC <dir>] [INC <dir>] [TST <dir>]
#                  [DOCS <dir>] [TOOLS <dir>] [TEMPLATES <dir>]
#                  [EXTRA_DIRS <dirs...>]
#                  [HEADERS_ONLY] [NO_TST])
#
# Register <target> for Doxygen documentation. Actual targets are created by
# apex_finalize_doxygen() which must be called once at the end of the root
# CMakeLists, after all add_subdirectory() calls. The deferred wiring lets each
# library's docs reference every other library's tag file (cross-module refs).
#
# Output: ${CMAKE_BINARY_DIR}/docs/lib<target>/doxygen/html
#
# Auto-detected directories (if they exist):
#   src, inc, tst, docs, tools, templates
#
# For custom directories, use EXTRA_DIRS.
# Respects PROJECT_BUILD_DOCS toggle.
# ------------------------------------------------------------------------------
function (apex_add_doxygen _target)
  if (NOT PROJECT_BUILD_DOCS)
    return()
  endif ()

  # Idempotent: skip if already registered.
  get_property(_already GLOBAL PROPERTY APEX_DOXYGEN_${_target}_REGISTERED)
  if (_already)
    return()
  endif ()

  cmake_parse_arguments(
    DOX "HEADERS_ONLY;NO_TST" "README;SRC;INC;TST;DOCS;TOOLS;TEMPLATES" "EXTRA_DIRS" ${ARGN}
  )

  # Defaults - auto-detect standard directories
  if (DOX_HEADERS_ONLY)
    set(DOX_SRC "")
  elseif (NOT DOX_SRC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set(DOX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  endif ()

  if (NOT DOX_INC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/inc")
    set(DOX_INC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
  endif ()

  if (DOX_NO_TST)
    set(DOX_TST "")
  elseif (NOT DOX_TST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tst")
    set(DOX_TST "${CMAKE_CURRENT_SOURCE_DIR}/tst")
  endif ()

  if (NOT DOX_DOCS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    set(DOX_DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
  endif ()

  if (NOT DOX_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    set(DOX_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/tools")
  endif ()

  if (NOT DOX_TEMPLATES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/templates")
    set(DOX_TEMPLATES "${CMAKE_CURRENT_SOURCE_DIR}/templates")
  endif ()

  if (NOT DOX_README AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    # _cuda variants share the same source dir as their parent lib; auto-
    # including the same README would create duplicate-anchor warnings when
    # tag files are cross-loaded. The parent lib owns the README.
    if (NOT _target MATCHES "_cuda$")
      set(DOX_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    endif ()
  endif ()

  # Stash all the per-target state in GLOBAL properties for apex_finalize_doxygen.
  set_property(GLOBAL APPEND PROPERTY APEX_DOXYGEN_LIBS "${_target}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_REGISTERED TRUE)
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_SRC "${DOX_SRC}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_INC "${DOX_INC}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TST "${DOX_TST}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_DOCS "${DOX_DOCS}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TOOLS "${DOX_TOOLS}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TEMPLATES "${DOX_TEMPLATES}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_EXTRA_DIRS "${DOX_EXTRA_DIRS}")
  set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_README "${DOX_README}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_finalize_doxygen()
#
# After all apex_add_doxygen() calls have registered their libraries, this
# function generates the actual Doxygen targets. Each library gets a two-pass
# pipeline:
#
#   Pass 1 (<target>_doctags): doxygen runs with GENERATE_HTML=NO, producing
#     only the .tag file. Fast; no sibling deps.
#
#   Pass 2 (<target>_docs): doxygen runs with GENERATE_HTML=YES and TAGFILES
#     populated with every other library's .tag file. Depends on every Pass 1.
#
# Result: cross-module markdown links like [Foo](../foo/README.md) resolve
# cleanly through doxygen's tag-file mechanism. Must be called from the root
# CMakeLists.txt after every add_subdirectory() that may register doxygen libs.
# ------------------------------------------------------------------------------
function (apex_finalize_doxygen)
  if (NOT PROJECT_BUILD_DOCS)
    return()
  endif ()

  find_package(Doxygen QUIET)
  if (NOT DOXYGEN_FOUND)
    message(WARNING "Doxygen not found; no docs targets created")
    return()
  endif ()

  # Auto-discover parent-dir READMEs that no registered library owns. Cross-
  # module links like [Foo](../foo/README.md) need a tag file for every README
  # they target; aggregator dirs (those that only host add_subdirectory calls
  # and have no library of their own) would otherwise be unresolvable. We give
  # each such README a synthetic README-only doxygen scope so its anchors land
  # in a tag file and resolve like any other module's.
  set(_covered_readmes "")
  get_property(_libs GLOBAL PROPERTY APEX_DOXYGEN_LIBS)
  foreach (_lib IN LISTS _libs)
    get_property(_readme GLOBAL PROPERTY APEX_DOXYGEN_${_lib}_README)
    if (_readme AND NOT _readme STREQUAL "")
      list(APPEND _covered_readmes "${_readme}")
    endif ()
  endforeach ()

  foreach (_root IN ITEMS src apps)
    file(GLOB_RECURSE _readmes "${CMAKE_SOURCE_DIR}/${_root}/*/README.md")
    foreach (_readme IN LISTS _readmes)
      list(FIND _covered_readmes "${_readme}" _idx)
      if (NOT _idx EQUAL -1)
        continue()
      endif ()
      get_filename_component(_dir "${_readme}" DIRECTORY)

      # Prefer to ATTACH this orphan README to a sole descendant lib instead
      # of creating a synthetic scope. That collapses the "<lib>" vs
      # "<lib> (overview)" duality on the landing page when a lib lives in a
      # sub-subdir whose parent owns the prose README. Rules:
      #   - The descendant lib's source dir must be a strict child of _dir.
      #   - `*_cuda` siblings are ignored for the count (they share the parent
      #     lib's source dir and would otherwise prevent the merge).
      #   - Exactly one qualifying lib -> attach. Zero or two-or-more -> synth.
      set(_descendants "")
      foreach (_lib IN LISTS _libs)
        if (_lib MATCHES "_cuda$")
          continue()
        endif ()
        get_property(_lib_src GLOBAL PROPERTY APEX_DOXYGEN_${_lib}_SOURCE_DIR)
        if (_lib_src
            AND NOT _lib_src STREQUAL "${_dir}"
            AND _lib_src MATCHES "^${_dir}/"
        )
          list(APPEND _descendants "${_lib}")
        endif ()
      endforeach ()

      list(LENGTH _descendants _ndesc)
      if (_ndesc EQUAL 1)
        # Attach the parent README to that sole real lib. Its prose lands on
        # the user-facing landing-page entry instead of in a synth scope.
        list(GET _descendants 0 _adoptee)
        set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_adoptee}_README "${_readme}")
        list(APPEND _covered_readmes "${_readme}")
        continue()
      endif ()

      file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}/${_root}" "${_dir}")
      string(REPLACE "/" "_" _synth "${_root}_${_rel}")
      if (NOT _synth)
        continue()
      endif ()
      get_property(_already GLOBAL PROPERTY APEX_DOXYGEN_${_synth}_REGISTERED)
      if (_already)
        continue()
      endif ()
      set_property(GLOBAL APPEND PROPERTY APEX_DOXYGEN_LIBS "${_synth}")
      set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_synth}_REGISTERED TRUE)
      set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_synth}_SOURCE_DIR "${_dir}")
      set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_synth}_README "${_readme}")
      # Pull in a sibling docs/ folder if present so links like
      # [Foo](docs/FOO.md) resolve within this scope.
      if (EXISTS "${_dir}/docs")
        set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_synth}_DOCS "${_dir}/docs")
      endif ()
      # Other state fields stay empty for README-only scopes.
    endforeach ()
  endforeach ()

  get_property(_libs GLOBAL PROPERTY APEX_DOXYGEN_LIBS)
  if (NOT _libs)
    return()
  endif ()

  _apex_fetch_doxygen_awesome()

  set(_doxyfile_in "${CMAKE_SOURCE_DIR}/docs/Doxyfile.common.in")
  if (NOT EXISTS "${_doxyfile_in}")
    message(WARNING "Doxygen template not found: ${_doxyfile_in} -- no docs targets created")
    return()
  endif ()

  list(REMOVE_DUPLICATES _libs)

  # First sweep: configure two Doxyfiles per lib (tag-only + full) and the
  # tag-only custom command. Tag generation has no sibling dependency.
  foreach (_target IN LISTS _libs)
    get_property(_src GLOBAL PROPERTY APEX_DOXYGEN_${_target}_SRC)
    get_property(_inc GLOBAL PROPERTY APEX_DOXYGEN_${_target}_INC)
    get_property(_tst GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TST)
    get_property(_docs GLOBAL PROPERTY APEX_DOXYGEN_${_target}_DOCS)
    get_property(_tools GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TOOLS)
    get_property(_tplt GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TEMPLATES)
    get_property(_extras GLOBAL PROPERTY APEX_DOXYGEN_${_target}_EXTRA_DIRS)
    get_property(_readme GLOBAL PROPERTY APEX_DOXYGEN_${_target}_README)

    set(LIB_NAME "${_target}")
    set(DOCS_BASE "${CMAKE_BINARY_DIR}/docs/lib${_target}")
    set(DOCS_DIR "${DOCS_BASE}/doxygen/html")
    set(SRC_DIR "${_src}")
    set(INC_DIR "${_inc}")
    set(TST_DIR "${_tst}")
    set(DOCS_SRC_DIR "${_docs}")
    set(TOOLS_DIR "${_tools}")
    set(TEMPLATES_DIR "${_tplt}")
    set(EXTRA_DIRS "${_extras}")
    set(README_FILE "${_readme}")

    # Build the cross-library TAGFILES list (every other lib). Pass-1 leaves
    # this empty so doxygen never tries to read a not-yet-generated tag file.
    set(_tagfiles_list "")
    foreach (_other IN LISTS _libs)
      if (NOT _other STREQUAL _target)
        string(
          APPEND
          _tagfiles_list
          " \"${CMAKE_BINARY_DIR}/docs/lib${_other}/doxygen/html/${_other}.tag=../../lib${_other}/doxygen/html\""
        )
      endif ()
    endforeach ()

    # Pass 1: tag-only Doxyfile. No TAGFILES loaded yet, so cross-module
    # \ref resolution would fail; silence doc-error warnings for this pass.
    set(GENERATE_HTML "NO")
    set(GENERATE_TAGFILE_VALUE "${DOCS_DIR}/${LIB_NAME}.tag")
    set(TAGFILES_LIST "")
    set(WARN_IF_DOC_ERROR "NO")
    configure_file("${_doxyfile_in}" "${DOCS_BASE}/Doxyfile.tags" @ONLY)

    # Pass 2: full HTML Doxyfile with cross-library TAGFILES. Refs resolve;
    # WARN_IF_DOC_ERROR stays on so genuine drift surfaces.
    set(GENERATE_HTML "YES")
    set(GENERATE_TAGFILE_VALUE "")
    set(TAGFILES_LIST "${_tagfiles_list}")
    set(WARN_IF_DOC_ERROR "YES")
    configure_file("${_doxyfile_in}" "${DOCS_BASE}/Doxyfile" @ONLY)

    set(_tag_file "${DOCS_DIR}/${LIB_NAME}.tag")
    set(_docs_stamp "${DOCS_BASE}/.doxygen.stamp")

    set(_tag_deps "${DOCS_BASE}/Doxyfile.tags")
    if (README_FILE AND NOT README_FILE STREQUAL "")
      list(APPEND _tag_deps "${README_FILE}")
    endif ()

    add_custom_command(
      OUTPUT "${_tag_file}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${DOCS_DIR}"
      COMMAND "${DOXYGEN_EXECUTABLE}" "${DOCS_BASE}/Doxyfile.tags"
      DEPENDS ${_tag_deps}
      COMMENT "Doxygen tags: ${_target}"
      VERBATIM
    )

    # Stash per-target state so the HTML pass can read it back without
    # re-reading global properties.
    set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TAG_FILE "${_tag_file}")
    set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_DOCS_BASE "${DOCS_BASE}")
    set_property(GLOBAL PROPERTY APEX_DOXYGEN_${_target}_DOCS_STAMP "${_docs_stamp}")
  endforeach ()

  # Collect every lib's tag file so each HTML pass depends on all of them.
  set(_all_tag_files "")
  foreach (_target IN LISTS _libs)
    get_property(_tag_file GLOBAL PROPERTY APEX_DOXYGEN_${_target}_TAG_FILE)
    list(APPEND _all_tag_files "${_tag_file}")
  endforeach ()

  # Second sweep: HTML pass custom commands + per-lib docs targets + wire to
  # the global docs target.
  foreach (_target IN LISTS _libs)
    get_property(_docs_base GLOBAL PROPERTY APEX_DOXYGEN_${_target}_DOCS_BASE)
    get_property(_docs_stamp GLOBAL PROPERTY APEX_DOXYGEN_${_target}_DOCS_STAMP)
    get_property(_readme GLOBAL PROPERTY APEX_DOXYGEN_${_target}_README)
    set(DOCS_DIR "${_docs_base}/doxygen/html")

    set(_html_deps "${_docs_base}/Doxyfile" ${_all_tag_files})
    if (_readme AND NOT _readme STREQUAL "")
      list(APPEND _html_deps "${_readme}")
    endif ()

    if (TARGET "${_target}")
      get_target_property(_tgt_type "${_target}" TYPE)
      # Only file-producing targets have a $<TARGET_FILE:>; INTERFACE / OBJECT /
      # UTILITY targets (e.g. a custom docs target) do not, and referencing it
      # for them aborts generation.
      if (_tgt_type STREQUAL "EXECUTABLE"
          OR _tgt_type STREQUAL "STATIC_LIBRARY"
          OR _tgt_type STREQUAL "SHARED_LIBRARY"
          OR _tgt_type STREQUAL "MODULE_LIBRARY"
      )
        list(APPEND _html_deps "$<TARGET_FILE:${_target}>")
      endif ()
    endif ()

    if (_readme AND NOT _readme STREQUAL "")
      add_custom_command(
        OUTPUT "${_docs_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DOCS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_readme}" "${_docs_base}/README.md"
        COMMAND "${DOXYGEN_EXECUTABLE}" "${_docs_base}/Doxyfile"
        COMMAND ${CMAKE_COMMAND} -E touch "${_docs_stamp}"
        DEPENDS ${_html_deps}
        COMMENT "Doxygen: ${_target} -> ${DOCS_DIR}"
        VERBATIM
      )
    else ()
      add_custom_command(
        OUTPUT "${_docs_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DOCS_DIR}"
        COMMAND "${DOXYGEN_EXECUTABLE}" "${_docs_base}/Doxyfile"
        COMMAND ${CMAKE_COMMAND} -E touch "${_docs_stamp}"
        DEPENDS ${_html_deps}
        COMMENT "Doxygen: ${_target} -> ${DOCS_DIR}"
        VERBATIM
      )
    endif ()

    add_custom_target(${_target}_docs DEPENDS "${_docs_stamp}")
    if (TARGET "${_target}")
      add_dependencies(${_target}_docs ${_target})
    endif ()

    if (TARGET docs)
      add_dependencies(docs ${_target}_docs)
    endif ()

    set_property(
      TARGET ${_target}_docs
      APPEND
      PROPERTY
        ADDITIONAL_CLEAN_FILES
        "${DOCS_DIR};${_docs_base}/Doxyfile;${_docs_base}/Doxyfile.tags;${_docs_base}/README.md;${_docs_stamp}"
    )
  endforeach ()

  _apex_write_docs_index("${_libs}")
endfunction ()

# ------------------------------------------------------------------------------
# _apex_write_docs_index(<libs>)
#
# Generate the project-level docs landing page at ${CMAKE_BINARY_DIR}/docs/
# index.html. Areas are derived from each lib's source-dir position (the
# directory directly under src/, demos/, or apps/) -- the page has zero hardcoded
# module names, so new top-level dirs show up as new sections automatically.
# Re-written at configure time whenever the registered-lib set changes.
# ------------------------------------------------------------------------------
function (_apex_write_docs_index _libs)
  # Per-area buckets are dynamic: _grp_<area> holds libs for area <area>.
  # Area for each lib is derived from its SOURCE_DIR position (the dir
  # directly under src/, demos/, or apps/).
  set(_areas "")
  foreach (_lib IN LISTS _libs)
    get_property(_src GLOBAL PROPERTY APEX_DOXYGEN_${_lib}_SOURCE_DIR)
    set(_area "other")
    if (_src)
      file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_src}")
      if (_rel MATCHES "^([^/]+)/([^/]+)")
        # Area is "<root>/<group>" purely so libs sharing a root sort
        # contiguously on the landing page. We don't care what those root
        # names are -- they just keep apps from being interleaved with
        # library areas alphabetically. Display strips the root prefix.
        set(_area "${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
      endif ()
    endif ()
    if (NOT "${_area}" IN_LIST _areas)
      list(APPEND _areas "${_area}")
    endif ()
    list(APPEND _grp_${_area} "${_lib}")
  endforeach ()
  list(SORT _areas)
  if ("other" IN_LIST _areas)
    list(REMOVE_ITEM _areas "other")
    list(APPEND _areas "other")
  endif ()

  set(_index "${CMAKE_BINARY_DIR}/docs/index.html")
  set(_html
      "<!DOCTYPE html>
<html lang=\"en\">
<head>
<meta charset=\"utf-8\">
<title>apex_csf documentation</title>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; max-width: 900px; margin: 40px auto; padding: 0 24px; color: #333; line-height: 1.5; }
  h1 { border-bottom: 2px solid #1e88e5; padding-bottom: 6px; }
  h2 { margin-top: 2em; color: #1e88e5; border-bottom: 1px solid #e0e0e0; padding-bottom: 4px; }
  ul { list-style: none; padding-left: 0; column-count: 2; column-gap: 32px; }
  li { padding: 4px 0; break-inside: avoid; }
  a { color: #1565c0; text-decoration: none; }
  a:hover { text-decoration: underline; }
  code { background: #f5f5f5; padding: 1px 5px; border-radius: 3px; font-size: 0.92em; }
  .empty { color: #999; font-style: italic; }
</style>
</head>
<body>
<h1>apex_csf documentation</h1>
<p>Per-library API reference generated by Doxygen. Pick a module below.</p>
"
  )

  foreach (_area IN LISTS _areas)
    set(_libs_in_group ${_grp_${_area}})
    if (NOT _libs_in_group)
      continue()
    endif ()
    list(SORT _libs_in_group)

    # Display title: strip root sort prefix, snake_case -> "Title Case".
    string(REGEX REPLACE "^[^/]+/" "" _area_display "${_area}")
    string(REPLACE "_" ";" _words "${_area_display}")
    set(_title "")
    foreach (_w IN LISTS _words)
      string(SUBSTRING "${_w}" 0 1 _head)
      string(SUBSTRING "${_w}" 1 -1 _tail)
      string(TOUPPER "${_head}" _head)
      if (_title)
        string(APPEND _title " ")
      endif ()
      string(APPEND _title "${_head}${_tail}")
    endforeach ()

    string(APPEND _html "<h2>${_title}</h2>\n<ul>\n")
    foreach (_lib IN LISTS _libs_in_group)
      # Synthetic auto-discovered scopes are prefixed `src_` or `apps_` and
      # document an aggregator dir's README rather than a library. Display
      # them with the prefix stripped + an "(overview)" tag so they don't
      # look like duplicate entries next to their real-library siblings.
      if (_lib MATCHES "^(src|apps)_")
        string(REGEX REPLACE "^(src|apps)_" "" _display "${_lib}")
        string(
          APPEND
          _html
          "  <li><a href=\"lib${_lib}/doxygen/html/index.html\"><code>${_display}</code> <em>(overview)</em></a></li>\n"
        )
      else ()
        string(
          APPEND _html
          "  <li><a href=\"lib${_lib}/doxygen/html/index.html\"><code>${_lib}</code></a></li>\n"
        )
      endif ()
    endforeach ()
    string(APPEND _html "</ul>\n")
  endforeach ()

  string(APPEND _html "</body>
</html>
"
  )

  file(WRITE "${_index}" "${_html}")

  if (TARGET docs)
    set_property(
      TARGET docs
      APPEND
      PROPERTY ADDITIONAL_CLEAN_FILES "${_index}"
    )
  endif ()
endfunction ()
