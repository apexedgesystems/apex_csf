# ==============================================================================
# cmake/apex/test/build_info/fixture.cmake - apex/BuildInfo generator contract
#
# Runs the real generator (apex/BuildInfo.cmake in script mode) against
# repositories this script creates, so the identity's promises can be checked
# from the outside: the tree state is a function of tracked content alone
# (diff presentation settings do not move it, a textconv driver cannot hide
# an edit, a touched file is not a modification), and the description takes
# its documented forms (an exact tag, tag-N-g<12 digits> whatever core.abbrev
# says, a bare hash).
#
# Usage: cmake -DMODULE=<BuildInfo.cmake> -DTEMPLATE=<BuildInfo.cpp.in>
#              -DGIT_EXECUTABLE=<git> -DWORK=<scratch dir> -P fixture.cmake
# Exit status is the verdict; each check prints PASS or FAIL with its values.
# ==============================================================================
cmake_minimum_required(VERSION 3.24)

foreach (_v MODULE TEMPLATE GIT_EXECUTABLE WORK)
  if (NOT DEFINED ${_v})
    message(FATAL_ERROR "fixture: ${_v} is required")
  endif ()
endforeach ()

set(FAILURES 0)

function (expect_eq _name _actual _expected)
  if (_actual STREQUAL "${_expected}")
    message(STATUS "PASS ${_name}: ${_actual}")
  else ()
    message(STATUS "FAIL ${_name}: got '${_actual}', expected '${_expected}'")
    math(EXPR _f "${FAILURES} + 1")
    set(FAILURES
        ${_f}
        PARENT_SCOPE
    )
  endif ()
endfunction ()

function (expect_ne _name _actual _unexpected)
  if (NOT _actual STREQUAL "${_unexpected}")
    message(STATUS "PASS ${_name}: ${_actual}")
  else ()
    message(STATUS "FAIL ${_name}: got '${_actual}', which must differ from '${_unexpected}'")
    math(EXPR _f "${FAILURES} + 1")
    set(FAILURES
        ${_f}
        PARENT_SCOPE
    )
  endif ()
endfunction ()

function (expect_match _name _actual _regex)
  if (_actual MATCHES "${_regex}")
    message(STATUS "PASS ${_name}: ${_actual}")
  else ()
    message(STATUS "FAIL ${_name}: got '${_actual}', expected to match '${_regex}'")
    math(EXPR _f "${FAILURES} + 1")
    set(FAILURES
        ${_f}
        PARENT_SCOPE
    )
  endif ()
endfunction ()

set(REPO "${WORK}/repo")
file(REMOVE_RECURSE "${REPO}")
file(MAKE_DIRECTORY "${REPO}")

function (repo_git)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REPO}" ${ARGN}
    RESULT_VARIABLE _r
    OUTPUT_QUIET ERROR_QUIET
  )
  if (NOT _r EQUAL 0)
    message(FATAL_ERROR "git ${ARGN} failed")
  endif ()
endfunction ()

# Run the generator against the fixture repository and read one field back.
function (identity_field _name _out)
  set(_outfile "${WORK}/out.cpp")
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -DSOURCE_DIR=${REPO} -DTEMPLATE=${TEMPLATE} -DOUTPUT=${_outfile}
      -DPROJECT_NAME=fixture -DPROJECT_VERSION=0.0.0 -DBUILD_TYPE=Debug -DCOMPILER=x
      -DTARGET_SYSTEM=y -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -P "${MODULE}"
    RESULT_VARIABLE _r
    OUTPUT_QUIET
    ERROR_VARIABLE _e
  )
  if (NOT _r EQUAL 0)
    message(FATAL_ERROR "generator failed: ${_e}")
  endif ()
  file(READ "${_outfile}" _unit)
  string(REGEX MATCH " ${_name} = ([^;]*);" _m "${_unit}")
  string(REPLACE "\"" "" _value "${CMAKE_MATCH_1}")
  set(${_out}
      "${_value}"
      PARENT_SCOPE
  )
endfunction ()

repo_git(init -q -b main .)
repo_git(config user.email fixture@example.invalid)
repo_git(config user.name fixture)
repo_git(config commit.gpgsign false)
file(WRITE "${REPO}/text.txt" "one\ntwo\nthree\n")
file(WRITE "${REPO}/blob.bin" "binary-v1\n")
repo_git(add -A)
repo_git(commit -q -m first)

# ------------------------------------------------------------------------------
# Description forms
# ------------------------------------------------------------------------------
repo_git(tag v9.9.9)
identity_field(DESCRIBE _d)
identity_field(COMMIT _c)
expect_eq(exact-tag "${_d}" "v9.9.9")
expect_match(commit-twelve-hex "${_c}" "^[0-9a-f]+$")
string(LENGTH "${_c}" _clen)
expect_eq(commit-length "${_clen}" "12")
file(APPEND "${REPO}/text.txt" "four\n")
repo_git(commit -q -am second)
repo_git(config core.abbrev 5)
identity_field(DESCRIBE _d)
identity_field(COMMIT _c)
expect_eq(tag-n-g12-under-short-abbrev "${_d}" "v9.9.9-1-g${_c}")
repo_git(config --unset core.abbrev)

# ------------------------------------------------------------------------------
# Tree state follows content, not presentation
# ------------------------------------------------------------------------------
identity_field(TREE_STATE _t0)
expect_eq(clean-after-commit "${_t0}" "clean")
file(APPEND "${REPO}/text.txt" "five\n")
identity_field(TREE_STATE _t1)
expect_ne(dirty-on-edit "${_t1}" "clean")
repo_git(config diff.context 0)
identity_field(TREE_STATE _t2)
repo_git(config --unset diff.context)
expect_eq(diff-context-does-not-move-state "${_t2}" "${_t1}")
repo_git(config diff.noprefix true)
identity_field(TREE_STATE _t3)
repo_git(config --unset diff.noprefix)
expect_eq(diff-noprefix-does-not-move-state "${_t3}" "${_t1}")
repo_git(checkout -q -- text.txt)
identity_field(TREE_STATE _t4)
expect_eq(clean-after-restore "${_t4}" "clean")

# ------------------------------------------------------------------------------
# A textconv driver cannot hide a tracked edit
# ------------------------------------------------------------------------------
file(WRITE "${REPO}/.gitattributes" "*.bin diff=const\n")
repo_git(add .gitattributes)
repo_git(commit -q -m attributes)
repo_git(config diff.const.textconv "echo constant #")
file(WRITE "${REPO}/blob.bin" "binary-v2\n")
identity_field(TREE_STATE _t5)
identity_field(DESCRIBE _d5)
identity_field(DIRTY _dirty5)
repo_git(config --unset diff.const.textconv)
expect_ne(textconv-cannot-hide-edit "${_t5}" "clean")
expect_eq(dirty-flag-on-textconv-edit "${_dirty5}" "true")
expect_match(describe-dirty-on-textconv-edit "${_d5}" "-dirty$")

# ------------------------------------------------------------------------------
# A stat-only change is not a modification
# ------------------------------------------------------------------------------
repo_git(checkout -q -- blob.bin)
file(TOUCH "${REPO}/text.txt")
identity_field(TREE_STATE _t6)
expect_eq(touch-is-not-dirty "${_t6}" "clean")

if (FAILURES GREATER 0)
  message(FATAL_ERROR "${FAILURES} check(s) failed")
endif ()
message(STATUS "all checks passed")
