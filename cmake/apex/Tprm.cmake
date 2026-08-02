# ==============================================================================
# Tprm.cmake - build-time TPRM generation from per-app manifests
#
# apex_add_tprm(NAME <App> MANIFEST <path>/tprm.manifest [EXEC <target>])
#
# The manifest is the packing recipe: named component groups (fullUid ->
# TOML source), named sequence groups (rts/ats slot -> sequence binary),
# and master blocks composing them. Payloads are generated with cfg2bin
# and packed with tprm_pack into ${CMAKE_CURRENT_BINARY_DIR}/tprm/, so
# no generated binary is ever committed; deployments reference products
# as <App>/<master> (see apex_add_deployment TPRM / TPRM_FALLBACK).
#
# Manifest grammar (flat lines, '#' comments, groups defined before use):
#   [components <name>]     followed by:  0x<fullUid>  <toml-path>
#   [sequences <name>]      followed by:  rts|ats <slot>  <file-path>
#   [master <file.tprm>]    followed by:  use components <name>
#                                         use sequences <name>
#                                         ...or direct entry/slot lines
#
# Composition is set union with NO shadowing: a fullUid or slot arriving
# twice in one master is a configure error. What you read is what packs.
#
# Sequence slot lines carry decimal slots and TOML sources; each
# compiles to ${gen}/rts|ats/{slot:03d}.rts/.ats -- the bank-directory
# naming the executive and action engine consume. Every [sequences]
# group compiles whether or not a master packs it, so the generated
# tree always carries the app's full mission bank (onboard sequencing
# is mission-critical: the manifest is how a mission's sequence set
# ships). A master that packs a sequence stores it under the reserved
# fullUid ranges for extraction on the target.
#
# EXEC names the executive target that compiles the generated
# constraint registry (fullUid -> legal-range table, assembled from
# each payload's constraint rows): the on-board rail that rejects an
# out-of-range payload even when ground tooling was bypassed. Without
# EXEC the registry is not generated and the reader's weak lookup
# stays null -- identity and integrity checks still run.
# ==============================================================================

# Parse one manifest into generation rules and register the products.
function (apex_add_tprm)
  cmake_parse_arguments(ARG "" "NAME;MANIFEST;EXEC" "" ${ARGN})
  if (NOT ARG_NAME OR NOT ARG_MANIFEST)
    message(FATAL_ERROR "apex_add_tprm: NAME and MANIFEST are required")
  endif ()

  # TPRM products feed POSIX deployments; bare-metal configures skip them
  # (the rust tools are not built there either).
  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  # Lean release builders configure with APEX_BUILD_TOOLS=OFF: no rust
  # tools, so no TPRM generation -- their trees ship binaries, and the
  # packaging that installs generated masters runs in tool-bearing
  # configures. A tools-enabled configure that still lacks the target
  # is a broken environment, not a policy, and fails loudly here
  # instead of as a phantom file dependency at build time.
  if (NOT TARGET ${PROJECT_NAME}_rust_tools)
    if (APEX_BUILD_TOOLS)
      message(
        FATAL_ERROR
          "apex_add_tprm(${ARG_NAME}): the rust tools target is missing in a tools-enabled configure (cargo not found?)"
      )
    endif ()
    return()
  endif ()

  get_filename_component(_manifest "${ARG_MANIFEST}" ABSOLUTE)
  if (NOT EXISTS "${_manifest}")
    message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): no manifest at ${_manifest}")
  endif ()
  get_filename_component(_src_dir "${_manifest}" DIRECTORY)
  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/tprm")
  set(_tools_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/rust")

  file(STRINGS "${_manifest}" _lines)

  set(_section "")
  set(_section_name "")
  set(_masters "")
  set(_all_tomls "")
  set(_all_seq_items "")
  set(_rows_pairs "")
  set(_rows_files "")

  foreach (_raw IN LISTS _lines)
    # Strip comments and surrounding whitespace; skip blanks.
    string(REGEX REPLACE "#.*$" "" _line "${_raw}")
    string(STRIP "${_line}" _line)
    if (_line STREQUAL "")
      continue()
    endif ()

    if (_line MATCHES "^\\[components +([A-Za-z0-9_]+)\\]$")
      set(_section "components")
      set(_section_name "${CMAKE_MATCH_1}")
      set(_grp_c_${_section_name} "")
    elseif (_line MATCHES "^\\[sequences +([A-Za-z0-9_]+)\\]$")
      set(_section "sequences")
      set(_section_name "${CMAKE_MATCH_1}")
      set(_grp_s_${_section_name} "")
    elseif (_line MATCHES "^\\[master +([A-Za-z0-9_.-]+)\\]$")
      set(_section "master")
      set(_section_name "${CMAKE_MATCH_1}")
      list(APPEND _masters "${_section_name}")
      set(_m_items_${_section_name} "")
    elseif (_line MATCHES "^use +components +([A-Za-z0-9_]+)$")
      if (NOT _section STREQUAL "master")
        message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): 'use' outside a master block: ${_raw}")
      endif ()
      if (NOT DEFINED _grp_c_${CMAKE_MATCH_1})
        message(
          FATAL_ERROR "apex_add_tprm(${ARG_NAME}): unknown components group '${CMAKE_MATCH_1}'"
        )
      endif ()
      list(APPEND _m_items_${_section_name} ${_grp_c_${CMAKE_MATCH_1}})
    elseif (_line MATCHES "^use +sequences +([A-Za-z0-9_]+)$")
      if (NOT _section STREQUAL "master")
        message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): 'use' outside a master block: ${_raw}")
      endif ()
      if (NOT DEFINED _grp_s_${CMAKE_MATCH_1})
        message(
          FATAL_ERROR "apex_add_tprm(${ARG_NAME}): unknown sequences group '${CMAKE_MATCH_1}'"
        )
      endif ()
      list(APPEND _m_items_${_section_name} ${_grp_s_${CMAKE_MATCH_1}})
    elseif (_line MATCHES "^(0x[0-9A-Fa-f]+) +(.+)$")
      set(_item "entry|${CMAKE_MATCH_1}|${CMAKE_MATCH_2}")
      if (_section STREQUAL "components")
        list(APPEND _grp_c_${_section_name} "${_item}")
      elseif (_section STREQUAL "master")
        list(APPEND _m_items_${_section_name} "${_item}")
      else ()
        message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): entry line outside a section: ${_raw}")
      endif ()
    elseif (_line MATCHES "^(rts|ats) +([0-9]+) +(.+)$")
      set(_item "${CMAKE_MATCH_1}|${CMAKE_MATCH_2}|${CMAKE_MATCH_3}")
      if (_section STREQUAL "sequences")
        list(APPEND _grp_s_${_section_name} "${_item}")
      elseif (_section STREQUAL "master")
        list(APPEND _m_items_${_section_name} "${_item}")
      else ()
        message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): sequence line outside a section: ${_raw}")
      endif ()
      list(APPEND _all_seq_items "${_item}")
    else ()
      message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): unparseable line: ${_raw}")
    endif ()
  endforeach ()

  if (NOT _masters)
    message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): manifest defines no [master] blocks")
  endif ()

  # Sequence sources compile to the bank-directory naming the executive
  # consumes ({slot:03d}.rts/.ats), whether or not a master packs them:
  # the generated tree always carries the app's full mission bank. One
  # slot per kind across the whole manifest -- the app has one bank.
  set(_seq_outputs "")
  set(_bank_rts "")
  set(_bank_ats "")
  foreach (_item IN LISTS _all_seq_items)
    string(REPLACE "|" ";" _f "${_item}")
    list(GET _f 0 _kind)
    list(GET _f 1 _slot)
    list(GET _f 2 _path)
    if (NOT _path MATCHES "\\.toml$")
      message(
        FATAL_ERROR "apex_add_tprm(${ARG_NAME}): sequence lines take TOML sources, got ${_path}"
      )
    endif ()
    set(_seq_toml "${_src_dir}/${_path}")
    if (NOT EXISTS "${_seq_toml}")
      message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): missing sequence source ${_path}")
    endif ()
    math(EXPR _slot "${_slot}")
    string(LENGTH "${_slot}" _len)
    if (_len LESS 3)
      math(EXPR _off "${_len} - 1")
      string(SUBSTRING "00${_slot}" ${_off} -1 _slot3)
    else ()
      set(_slot3 "${_slot}")
    endif ()
    set(_seq_out "${_gen_dir}/${_kind}/${_slot3}.${_kind}")
    if (DEFINED _seq_src_${_kind}_${_slot})
      if (NOT _seq_src_${_kind}_${_slot} STREQUAL "${_path}")
        message(
          FATAL_ERROR
            "apex_add_tprm(${ARG_NAME}): ${_kind} slot ${_slot} maps to both ${_seq_src_${_kind}_${_slot}} and ${_path}"
        )
      endif ()
    else ()
      set(_seq_src_${_kind}_${_slot} "${_path}")
      add_custom_command(
        OUTPUT "${_seq_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen_dir}/${_kind}"
        COMMAND "${_tools_dir}/cfg2bin" --config "${_seq_toml}" --output "${_seq_out}"
        DEPENDS "${_seq_toml}" ${PROJECT_NAME}_rust_tools
        COMMENT "[tprm] ${ARG_NAME}: ${_kind} ${_slot3} <- ${_path}"
        VERBATIM
      )
      list(APPEND _seq_outputs "${_seq_out}")
      if (_kind STREQUAL "rts")
        list(APPEND _bank_rts "${_seq_out}")
      else ()
        list(APPEND _bank_ats "${_seq_out}")
      endif ()
    endif ()
    set(_seq_out_${_kind}_${_slot} "${_seq_out}")
  endforeach ()

  # One cfg2bin rule per unique TOML source (payloads are shared between
  # masters that reference the same source).
  set(_master_outputs "")
  foreach (_master IN LISTS _masters)
    set(_pack_args "")
    set(_pack_deps "")
    set(_seen_keys "")

    foreach (_item IN LISTS _m_items_${_master})
      string(REPLACE "|" ";" _f "${_item}")
      list(GET _f 0 _kind)
      list(GET _f 1 _key)
      list(GET _f 2 _path)

      # No shadowing: a repeated fullUid or slot in one master is an error.
      set(_dedup "${_kind}:${_key}")
      if ("${_dedup}" IN_LIST _seen_keys)
        message(
          FATAL_ERROR
            "apex_add_tprm(${ARG_NAME}): ${_master} receives ${_dedup} twice -- composition is union without shadowing"
        )
      endif ()
      list(APPEND _seen_keys "${_dedup}")

      if (_kind STREQUAL "entry")
        set(_toml "${_src_dir}/${_path}")
        if (NOT EXISTS "${_toml}")
          message(FATAL_ERROR "apex_add_tprm(${ARG_NAME}): missing source ${_path} (${_master})")
        endif ()
        string(REGEX REPLACE "[/.]" "_" _stem "${_path}")
        set(_payload "${_gen_dir}/payloads/${_stem}.tprm")
        # v3 payloads are target-bound: the prelude carries the fullUid,
        # so one TOML source serves exactly one fullUid per app.
        if (NOT "${_toml}" IN_LIST _all_tomls)
          list(APPEND _all_tomls "${_toml}")
          set(_toml_uid_${_stem} "${_key}")
          add_custom_command(
            OUTPUT "${_payload}" "${_payload}.rows.json"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen_dir}/payloads"
            COMMAND "${_tools_dir}/cfg2bin" --config "${_toml}" --output "${_payload}" --fulluid
                    "${_key}" --constraint-rows "${_payload}.rows.json"
            DEPENDS "${_toml}" ${PROJECT_NAME}_rust_tools
            COMMENT "[tprm] ${ARG_NAME}: ${_path}"
            VERBATIM
          )
          list(APPEND _rows_pairs "${_key}|${_payload}.rows.json")
          list(APPEND _rows_files "${_payload}.rows.json")
        elseif (NOT _toml_uid_${_stem} STREQUAL "${_key}")
          message(
            FATAL_ERROR
              "apex_add_tprm(${ARG_NAME}): ${_path} serves both ${_toml_uid_${_stem}} and ${_key} -- v3 payloads are target-bound, one TOML per fullUid"
          )
        endif ()
        list(APPEND _pack_args "-e" "${_key}:${_payload}")
        list(APPEND _pack_deps "${_payload}")
      else ()
        math(EXPR _key "${_key}")
        set(_seq "${_seq_out_${_kind}_${_key}}")
        if (_kind STREQUAL "rts")
          list(APPEND _pack_args "-r" "${_key}:${_seq}")
        else ()
          list(APPEND _pack_args "-a" "${_key}:${_seq}")
        endif ()
        list(APPEND _pack_deps "${_seq}")
      endif ()
    endforeach ()

    set(_out "${_gen_dir}/${_master}")
    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen_dir}"
      COMMAND "${_tools_dir}/tprm_pack" pack ${_pack_args} -o "${_out}"
      DEPENDS ${_pack_deps} "${_manifest}" ${PROJECT_NAME}_rust_tools
      COMMENT "[tprm] ${ARG_NAME}: pack ${_master}"
      VERBATIM
    )
    list(APPEND _master_outputs "${_out}")

    # Deployments look products up as <App>/<master>.
    set_property(GLOBAL PROPERTY APEX_TPRM_PRODUCT_${ARG_NAME}/${_master} "${_out}")
  endforeach ()

  add_custom_target(apex_tprm_${ARG_NAME} ALL DEPENDS ${_master_outputs} ${_seq_outputs})
  set_property(GLOBAL PROPERTY APEX_TPRM_TARGET_${ARG_NAME} apex_tprm_${ARG_NAME})

  # Deployments stage the mission bank into bank_a alongside the master
  # (see apex_add_deployment).
  set_property(GLOBAL PROPERTY APEX_TPRM_BANK_RTS_${ARG_NAME} "${_bank_rts}")
  set_property(GLOBAL PROPERTY APEX_TPRM_BANK_ATS_${ARG_NAME} "${_bank_ats}")

  # On-board constraint registry: EXEC names the executive target that
  # compiles the generated fullUid -> table lookup. The registry is
  # assembled from each payload's constraint rows at build time; the
  # weak reference in TprmPayload.hpp resolves against it, so apps
  # without an EXEC (or without constraints) simply skip the rail.
  if (ARG_EXEC)
    set(_rows_manifest "${_gen_dir}/constraint_rows.txt")
    string(REPLACE ";" "\n" _rows_lines "${_rows_pairs}")
    file(WRITE "${_rows_manifest}" "${_rows_lines}\n")
    set(_registry "${_gen_dir}/${ARG_NAME}_tprm_constraints.cpp")
    add_custom_command(
      OUTPUT "${_registry}"
      COMMAND ${CMAKE_COMMAND} -DROWS_MANIFEST=${_rows_manifest} -DOUTPUT=${_registry} -P
              ${CMAKE_SOURCE_DIR}/cmake/apex/TprmConstraintGen.cmake
      DEPENDS ${_rows_files} "${_rows_manifest}"
              "${CMAKE_SOURCE_DIR}/cmake/apex/TprmConstraintGen.cmake"
      COMMENT "[tprm] ${ARG_NAME}: constraint registry"
      VERBATIM
    )
    target_sources(${ARG_EXEC} PRIVATE "${_registry}")
  endif ()
endfunction ()
