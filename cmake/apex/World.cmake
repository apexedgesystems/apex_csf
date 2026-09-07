# ==============================================================================
# apex/World.cmake - World bundle products
#
# apex_add_world() declares one simulated world: a packed, uid-stamped,
# hash-identified bundle (<body>.world.tprm) built from role-keyed
# content artifacts by the world_pack tool. Worlds are defined once and
# shared -- any deployment referencing WORLD <body> stages the same
# bundle, which is what lets two apps pin one earth.
#
# Sources are generated/gitignored artifacts, so world targets stay out
# of ALL: packing runs when a package (or the world_<body> target)
# demands it, and a missing source fails the pack loudly there -- a
# package that declares a world must contain it.
# ==============================================================================

# ------------------------------------------------------------------------------
# apex_add_world(BODY <name> COMPONENT_ID <hex> [GRAVITY <repo-rel>]
#                [TERRAIN <repo-rel>] [ATMOSPHERE <repo-rel>])
#
# COMPONENT_ID must sit in the reserved world range [0x0100, 0x01FF]
# (see sim_environment_world). Registers:
#   APEX_WORLD_PRODUCT_<body> - generated bundle path
#   APEX_WORLD_TARGET_<body>  - custom target that packs it
# ------------------------------------------------------------------------------
function (apex_add_world)
  cmake_parse_arguments(W "" "BODY;COMPONENT_ID;GRAVITY;TERRAIN;ATMOSPHERE" "" ${ARGN})
  apex_require(W_BODY W_COMPONENT_ID)

  if (APEX_PLATFORM_BAREMETAL)
    return()
  endif ()

  math(EXPR _id "${W_COMPONENT_ID}" OUTPUT_FORMAT DECIMAL)
  if (_id LESS 256 OR _id GREATER 511)
    message(
      FATAL_ERROR
        "apex_add_world(${W_BODY}): COMPONENT_ID ${W_COMPONENT_ID} outside the reserved world range [0x0100, 0x01FF]"
    )
  endif ()

  get_property(_dup GLOBAL PROPERTY APEX_WORLD_PRODUCT_${W_BODY})
  if (_dup)
    message(FATAL_ERROR "apex_add_world(${W_BODY}): world already defined (worlds are shared)")
  endif ()

  set(_out "${CMAKE_BINARY_DIR}/worlds/${W_BODY}.world.tprm")
  set(_args --out "${_out}" --body "${W_BODY}" --uid "${W_COMPONENT_ID}")
  set(_deps "")
  foreach (_role GRAVITY TERRAIN ATMOSPHERE)
    if (W_${_role})
      string(TOLOWER "${_role}" _flag)
      list(APPEND _args "--${_flag}" "${CMAKE_SOURCE_DIR}/${W_${_role}}")
      list(APPEND _deps "${CMAKE_SOURCE_DIR}/${W_${_role}}")
    endif ()
  endforeach ()

  add_custom_command(
    OUTPUT "${_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/worlds"
    COMMAND $<TARGET_FILE:world_pack> ${_args}
    DEPENDS world_pack ${_deps}
    COMMENT "[world] pack ${W_BODY} -> ${W_BODY}.world.tprm"
    VERBATIM
  )
  add_custom_target(world_${W_BODY} DEPENDS "${_out}")

  set_property(GLOBAL PROPERTY APEX_WORLD_PRODUCT_${W_BODY} "${_out}")
  set_property(GLOBAL PROPERTY APEX_WORLD_TARGET_${W_BODY} "world_${W_BODY}")
endfunction ()
