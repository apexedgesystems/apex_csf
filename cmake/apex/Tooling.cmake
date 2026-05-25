# ==============================================================================
# apex/Tooling.cmake - Documentation, compression, and static analysis
# ==============================================================================

include_guard(GLOBAL)

# ==============================================================================
# Doxygen Documentation
# ==============================================================================

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
      if (NOT _tgt_type STREQUAL "INTERFACE_LIBRARY" AND NOT _tgt_type STREQUAL "OBJECT_LIBRARY")
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
# index.html. Lists every registered library grouped by area (utilities, sim,
# system, apps), each linking into its per-library doxygen output. Re-written
# at configure time whenever the registered-lib set changes.
# ------------------------------------------------------------------------------
function (_apex_write_docs_index _libs)
  set(_grp_utilities "")
  set(_grp_sim "")
  set(_grp_system "")
  set(_grp_apps "")
  set(_grp_other "")

  foreach (_lib IN LISTS _libs)
    string(REGEX REPLACE "^src_" "" _key "${_lib}")
    if (_key MATCHES "^utilities")
      list(APPEND _grp_utilities "${_lib}")
    elseif (_key MATCHES "^sim")
      list(APPEND _grp_sim "${_lib}")
    elseif (_key MATCHES "^(system_core|executive|scheduler|hal|filesystem|system_component)")
      list(APPEND _grp_system "${_lib}")
    elseif (_lib MATCHES "^apps_")
      list(APPEND _grp_apps "${_lib}")
    else ()
      list(APPEND _grp_other "${_lib}")
    endif ()
  endforeach ()

  list(SORT _grp_utilities)
  list(SORT _grp_sim)
  list(SORT _grp_system)
  list(SORT _grp_apps)
  list(SORT _grp_other)

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

  foreach (_group_pair IN
           ITEMS "Utilities;_grp_utilities" "Simulation;_grp_sim" "System Core;_grp_system"
                 "Applications;_grp_apps" "Other;_grp_other"
  )
    string(REPLACE ";" ";" _kv "${_group_pair}")
    list(GET _kv 0 _title)
    list(GET _kv 1 _varname)
    set(_libs_in_group ${${_varname}})

    if (NOT _libs_in_group)
      continue()
    endif ()

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

# ==============================================================================
# UPX Compression
# ==============================================================================

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

# ==============================================================================
# Clang-Tidy for CUDA
# ==============================================================================

# Cache options
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
