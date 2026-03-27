# ==============================================================================
# ExternalDependencies.cmake - Third-party library fetching
# ==============================================================================

# Skip for bare-metal builds (no standard library available)
if (APEX_PLATFORM_BAREMETAL)
  return()
endif ()

# Include the FetchContent module to manage external dependencies.
include(FetchContent)

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

# Disable test registration for FetchContent dependencies. Vernier and Seeker
# test themselves in their own CI -- we only consume their libraries and tools.
set(BUILD_TESTING
    OFF
    CACHE BOOL "" FORCE
)

# Declare and make the Vernier benchmarking library available.
fetchcontent_declare(
  vernier
  SYSTEM
  GIT_REPOSITORY https://github.com/apexedgesystems/vernier.git
  GIT_TAG v1.0.1
)
set(VERNIER_BUILD_TOOLS
    ON
    CACHE BOOL "" FORCE
)
set(PROJECT_BUILD_DOCS
    OFF
    CACHE BOOL "" FORCE
)
fetchcontent_makeavailable(vernier)

# Declare and make the Seeker diagnostics library available.
fetchcontent_declare(
  seeker
  SYSTEM
  GIT_REPOSITORY https://github.com/apexedgesystems/seeker.git
  GIT_TAG v1.0.0
)
set(SEEKER_BUILD_TOOLS
    ON
    CACHE BOOL "" FORCE
)
fetchcontent_makeavailable(seeker)

# Re-enable testing for apex_csf's own tests.
set(BUILD_TESTING
    ON
    CACHE BOOL "" FORCE
)
include(CTest)
