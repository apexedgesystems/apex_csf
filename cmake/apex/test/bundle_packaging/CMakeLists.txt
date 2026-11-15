# ==============================================================================
# cmake/apex/test/bundle_packaging - apex_add_bundle contract fixture
#
# A small project that hands the real packaging module one bundle per CASE, so
# `make check-bundle-packaging` can assert from the outside what a bundle
# carries and what it refuses:
#
#   good              one deployment with a generated TPRM and one tool of each
#                     kind (C++ target, prebuilt executable, shell script,
#                     Python console script). check.sh packages it from an
#                     unbuilt tree and runs every tool from a copy elsewhere.
#   unknown-tool      a TOOLS name nothing provides fails the configure
#   library-as-tool   a TOOLS name that is a library target fails the configure
#   baremetal-tools   TOOLS in a bare-metal build fails the configure
#   baremetal-plain   a bundle without TOOLS in a bare-metal build configures
#
# FX_BIN_FILE lets check.sh register a prebuilt executable built for another
# architecture, and check.sh adds such a module to the generated Python library;
# the package step must refuse both.
# ==============================================================================

cmake_minimum_required(VERSION 3.24)
project(apex_bundle_packaging_fixture C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
include(apex/Core)
include(apex/Packaging)

if (NOT DEFINED CASE)
  message(
    FATAL_ERROR "pass -DCASE=<good|unknown-tool|library-as-tool|baremetal-tools|baremetal-plain>"
  )
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# The deployment: one executive and a master TPRM that only exists once its
# generating target has run, registered the way apex_add_tprm registers one.
add_executable(fx_exec src/fx_main.c)
target_compile_definitions(fx_exec PRIVATE FX_NAME="fx_exec")
set(_tprm "${CMAKE_BINARY_DIR}/tprm/master.tprm")
add_custom_command(
  OUTPUT "${_tprm}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/tprm"
  COMMAND ${CMAKE_COMMAND} -E echo "fixture master" > "${_tprm}"
  VERBATIM
)
add_custom_target(
  fx_tprm
  DEPENDS "${_tprm}"
)
set_property(GLOBAL PROPERTY APEX_TPRM_PRODUCT_FxApp/master.tprm "${_tprm}")
set_property(GLOBAL PROPERTY APEX_TPRM_TARGET_FxApp fx_tprm)
apex_add_deployment(NAME FxApp EXEC fx_exec TPRM FxApp/master.tprm)

# One tool of each kind.
add_library(fx_dep SHARED src/fx_dep.c)
set_target_properties(fx_dep PROPERTIES VERSION 1.0.0 SOVERSION 1)
add_executable(fx_cpp_tool src/fx_main.c)
target_compile_definitions(fx_cpp_tool PRIVATE FX_NAME="fx_cpp_tool" FX_USE_DEP)
target_link_libraries(
  fx_cpp_tool
  PRIVATE fx_dep
)
set_target_properties(fx_cpp_tool PROPERTIES INSTALL_RPATH "$ORIGIN/../libs")

add_executable(fx_native src/fx_main.c)
target_compile_definitions(fx_native PRIVATE FX_NAME="fx-bin")
set(FX_BIN_FILE
    "${CMAKE_BINARY_DIR}/bin/fx_native"
    CACHE FILEPATH "The prebuilt executable registered as fx-bin"
)
# An installed Python library, written the way an installer leaves one: fxpkg
# declares the console script fx-py and requires fxdep; `unrelated` is installed
# beside them and required by nothing, so a bundle must not carry it.
set(FX_PYLIB "${CMAKE_BINARY_DIR}/pylib")
file(WRITE "${FX_PYLIB}/fxpkg/__init__.py"
     "import sys\n\nimport fxdep\n\n\ndef main():\n"
     "    print('fx-py with %s as %s' % (fxdep.NAME, sys.argv[0]))\n    return 0\n"
)
file(WRITE "${FX_PYLIB}/fxdep/__init__.py" "NAME = 'fxdep'\n")
file(WRITE "${FX_PYLIB}/unrelated/__init__.py" "")
file(WRITE "${FX_PYLIB}/fxpkg-1.0.dist-info/METADATA"
     "Metadata-Version: 2.1\nName: fxpkg\nVersion: 1.0\nRequires-Python: >=3.8\n"
     "Requires-Dist: fxdep\nRequires-Dist: fxextra; extra == \"plots\"\n"
)
file(WRITE "${FX_PYLIB}/fxpkg-1.0.dist-info/entry_points.txt"
     "[console_scripts]\nfx-py = fxpkg:main\n"
)
file(WRITE "${FX_PYLIB}/fxpkg-1.0.dist-info/RECORD"
     "fxpkg/__init__.py,,\nfxpkg-1.0.dist-info/METADATA,,\n"
     "fxpkg-1.0.dist-info/entry_points.txt,,\nfxpkg-1.0.dist-info/RECORD,,\nbin/fx-py,,\n"
)
file(WRITE "${FX_PYLIB}/fxdep-2.0.dist-info/METADATA"
     "Metadata-Version: 2.1\nName: fxdep\nVersion: 2.0\n"
)
file(WRITE "${FX_PYLIB}/fxdep-2.0.dist-info/RECORD"
     "fxdep/__init__.py,,\nfxdep-2.0.dist-info/METADATA,,\nfxdep-2.0.dist-info/RECORD,,\n"
)
file(WRITE "${FX_PYLIB}/unrelated-3.0.dist-info/METADATA"
     "Metadata-Version: 2.1\nName: unrelated\nVersion: 3.0\n"
)
file(
  WRITE "${FX_PYLIB}/unrelated-3.0.dist-info/RECORD"
  "unrelated/__init__.py,,\nunrelated-3.0.dist-info/METADATA,,\nunrelated-3.0.dist-info/RECORD,,\n"
)

apex_register_tool(
  NAME
  fx-bin
  KIND
  bin
  FILE
  "${FX_BIN_FILE}"
  DEPENDS
  fx_native
)
apex_register_tool(
  NAME fx-script.sh KIND script FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/fx-script.sh"
)
apex_register_tool(
  NAME
  fx-py
  KIND
  py
  FILE
  "${FX_PYLIB}/bin/fx-py"
  PYTHON_LIB
  "${FX_PYLIB}"
)

if (CASE STREQUAL "good")
  set(_tools fx_cpp_tool fx-bin fx-script.sh fx-py)
elseif (CASE STREQUAL "unknown-tool")
  set(_tools fx_cpp_tool no-such-tool)
elseif (CASE STREQUAL "library-as-tool")
  set(_tools fx_dep)
elseif (CASE STREQUAL "baremetal-tools")
  set(APEX_PLATFORM_BAREMETAL ON)
  set(_tools fx_cpp_tool)
elseif (CASE STREQUAL "baremetal-plain")
  set(APEX_PLATFORM_BAREMETAL ON)
  set(_tools "")
else ()
  message(FATAL_ERROR "unknown CASE '${CASE}'")
endif ()

if (_tools)
  apex_add_bundle(NAME FxBundle DEPLOYMENTS FxApp TOOLS ${_tools})
else ()
  apex_add_bundle(NAME FxBundle DEPLOYMENTS FxApp)
endif ()

# The root project finalizes packages for hosted builds only.
if (NOT APEX_PLATFORM_BAREMETAL)
  apex_finalize_packages()
endif ()
