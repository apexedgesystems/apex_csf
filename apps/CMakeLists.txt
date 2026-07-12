# ==============================================================================
# apps/CMakeLists.txt - User production application space
#
# Any subdirectory containing a CMakeLists.txt is added automatically:
# adding an application never requires editing this file. Stock demo
# applications live under demos/; this tree is yours.
# ==============================================================================

file(
  GLOB _apex_user_apps
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/*"
)
list(SORT _apex_user_apps)
foreach (_app IN LISTS _apex_user_apps)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_app}/CMakeLists.txt")
    add_subdirectory("${_app}")
  endif ()
endforeach ()
