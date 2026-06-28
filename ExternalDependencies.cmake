# ==============================================================================
# ExternalDependencies.cmake - Third-party library fetching
# ==============================================================================

# Skip for bare-metal builds (no standard library available)
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# Include the FetchContent module to manage external dependencies.
include(FetchContent)

# Hermetic builds: when the dev image has baked the dependency sources at their
# pinned tags (docker/scripts/bake-external-deps.sh into $APEX_DEPS_DIR), point
# FetchContent at the local copies so configure never reaches GitHub. Absent the
# env (local dev), FetchContent clones normally. Names match the declarations
# below; FetchContent uppercases them for FETCHCONTENT_SOURCE_DIR_<NAME>.
if (DEFINED ENV{APEX_DEPS_DIR})
  foreach (_apex_dep fmt googletest vernier seeker)
    string(TOUPPER "${_apex_dep}" _apex_dep_uc)
    if (EXISTS "$ENV{APEX_DEPS_DIR}/${_apex_dep}")
      set(FETCHCONTENT_SOURCE_DIR_${_apex_dep_uc}
          "$ENV{APEX_DEPS_DIR}/${_apex_dep}"
          CACHE PATH "Baked source for ${_apex_dep} (hermetic build)" FORCE
      )
    endif ()
  endforeach ()
  unset(_apex_dep)
  unset(_apex_dep_uc)
endif ()

# Declare and make the fmt library available.
fetchcontent_declare(
  fmt
  SYSTEM
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG 11.1.4
)
fetchcontent_makeavailable(fmt)

# Declare and make the GoogleTest library available.
fetchcontent_declare(
  googletest
  SYSTEM
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.16.0
)
# Prevent GoogleTest from installing with your project
set(INSTALL_GTEST
    OFF
    CACHE BOOL "" FORCE
)
fetchcontent_makeavailable(googletest)

# Suppress vernier/seeker tests and docs during their fetch. Save apex_csf's
# own settings first; restore after. Use ON as fallback when the variable
# wasn't set yet at save time (typical first-configure path).
if (NOT DEFINED _apex_save_BUILD_TESTING)
  if (DEFINED BUILD_TESTING)
    set(_apex_save_BUILD_TESTING "${BUILD_TESTING}")
  else ()
    set(_apex_save_BUILD_TESTING ON)
  endif ()
endif ()
if (NOT DEFINED _apex_save_PROJECT_BUILD_DOCS)
  if (DEFINED PROJECT_BUILD_DOCS)
    set(_apex_save_PROJECT_BUILD_DOCS "${PROJECT_BUILD_DOCS}")
  else ()
    set(_apex_save_PROJECT_BUILD_DOCS ON)
  endif ()
endif ()
set(BUILD_TESTING
    OFF
    CACHE BOOL "" FORCE
)
set(PROJECT_BUILD_DOCS
    OFF
    CACHE BOOL "" FORCE
)

# Declare and make the Vernier benchmarking library available.
fetchcontent_declare(
  vernier
  SYSTEM
  GIT_REPOSITORY https://github.com/apexedgesystems/vernier.git
  GIT_TAG v1.0.2
)
set(VERNIER_BUILD_TOOLS
    ON
    CACHE BOOL "" FORCE
)
fetchcontent_makeavailable(vernier)

# Declare and make the Seeker diagnostics library available.
fetchcontent_declare(
  seeker
  SYSTEM
  GIT_REPOSITORY https://github.com/apexedgesystems/seeker.git
  GIT_TAG v1.0.1
)
set(SEEKER_BUILD_TOOLS
    ON
    CACHE BOOL "" FORCE
)
fetchcontent_makeavailable(seeker)

# Restore apex_csf's settings.
set(BUILD_TESTING
    "${_apex_save_BUILD_TESTING}"
    CACHE BOOL "" FORCE
)
set(PROJECT_BUILD_DOCS
    "${_apex_save_PROJECT_BUILD_DOCS}"
    CACHE BOOL "" FORCE
)
unset(_apex_save_BUILD_TESTING)
unset(_apex_save_PROJECT_BUILD_DOCS)
include(CTest)
