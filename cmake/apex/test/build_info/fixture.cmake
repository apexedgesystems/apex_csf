# ==============================================================================
# cmake/apex/test/build_info/fixture.cmake - apex/BuildInfo generator contract
#
# Runs the real generator (apex/BuildInfo.cmake in script mode) against
# repositories this script creates, so the identity's promises can be checked
# from the outside: the tree state is a function of tracked content alone
# (diff presentation settings and path quoting do not move it, a textconv
# driver cannot hide an edit, every path and object type counts, a touched
# file is not a modification), the description takes its documented forms
# with twelve commit digits under any core.abbrev, and the -dirty mark
# follows the tree state even when a tag's own name looks like a hash, a
# hash suffix or a dirty mark.
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

# A state that is neither clean nor unknown: a real tree id.
function (expect_state _name _actual)
  if (_actual
      MATCHES
      "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$"
  )
    message(STATUS "PASS ${_name}: ${_actual}")
  else ()
    message(STATUS "FAIL ${_name}: got '${_actual}', expected a twelve-digit tree state")
    math(EXPR _f "${FAILURES} + 1")
    set(FAILURES
        ${_f}
        PARENT_SCOPE
    )
  endif ()
endfunction ()

set(REPO "${WORK}/repo")
file(REMOVE_RECURSE "${REPO}" "${WORK}/sub")
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

function (commit_all _msg)
  repo_git(add -A)
  repo_git(commit -q -m "${_msg}")
endfunction ()

repo_git(init -q -b main .)
repo_git(config user.email fixture@example.invalid)
repo_git(config user.name fixture)
repo_git(config commit.gpgsign false)
file(WRITE "${REPO}/text.txt" "one\ntwo\nthree\n")
file(WRITE "${REPO}/blob.bin" "binary-v1\n")
commit_all(first)

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
commit_all(second)
repo_git(config core.abbrev 5)
identity_field(DESCRIBE _d)
identity_field(COMMIT _c)
expect_eq(tag-n-g12-under-short-abbrev "${_d}" "v9.9.9-1-g${_c}")
repo_git(config --unset core.abbrev)

# Tag names that look like a hash, a hash suffix or a dirty mark are still
# exact tags, clean and dirty alike; the -dirty mark follows the tree state.
foreach (_tag deadbeef release-golden release-dirty release-1-gdeadbeef)
  file(APPEND "${REPO}/text.txt" "for ${_tag}\n")
  commit_all("tag ${_tag}")
  repo_git(tag "${_tag}")
  identity_field(DESCRIBE _d)
  identity_field(DIRTY _dirty)
  expect_eq("exact-tag-${_tag}" "${_d}" "${_tag}")
  expect_eq("exact-tag-${_tag}-clean" "${_dirty}" "false")
  file(APPEND "${REPO}/text.txt" "edit under ${_tag}\n")
  identity_field(DESCRIBE _d)
  identity_field(DIRTY _dirty)
  expect_eq("exact-tag-${_tag}-dirty-mark" "${_d}" "${_tag}-dirty")
  expect_eq("exact-tag-${_tag}-dirty-flag" "${_dirty}" "true")
  repo_git(checkout -q -- text.txt)
endforeach ()

# ------------------------------------------------------------------------------
# Tree state follows content, not presentation
# ------------------------------------------------------------------------------
identity_field(TREE_STATE _t0)
expect_eq(clean-after-commits "${_t0}" "clean")
file(APPEND "${REPO}/text.txt" "five\n")
identity_field(TREE_STATE _t1)
expect_state(dirty-on-edit "${_t1}")
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
commit_all(attributes)
repo_git(config diff.const.textconv "echo constant #")
file(WRITE "${REPO}/blob.bin" "binary-v2\n")
identity_field(TREE_STATE _t5)
identity_field(DESCRIBE _d5)
identity_field(DIRTY _dirty5)
repo_git(config --unset diff.const.textconv)
expect_state(textconv-cannot-hide-edit "${_t5}")
expect_eq(dirty-flag-on-textconv-edit "${_dirty5}" "true")
expect_match(describe-dirty-on-textconv-edit "${_d5}" "-dirty$")
repo_git(checkout -q -- blob.bin)

# ------------------------------------------------------------------------------
# Every path counts, whatever it is called
# ------------------------------------------------------------------------------
file(WRITE "${REPO}/victim" "same bytes\n")
file(WRITE "${REPO}/victim;extra.txt" "same bytes\n")
file(WRITE "${REPO}/same|left.txt" "pipe\n")
file(WRITE "${REPO}/same|right.txt" "pipe\n")
file(WRITE "${REPO}/uñicode.txt" "utf-8 name\n")
commit_all(paths)
identity_field(TREE_STATE _tp0)
expect_eq(paths-clean "${_tp0}" "clean")
file(APPEND "${REPO}/victim;extra.txt" "edited\n")
identity_field(TREE_STATE _tp1)
identity_field(DIRTY _dp1)
expect_state(semicolon-path-edit-counts "${_tp1}")
expect_eq(semicolon-path-edit-dirty "${_dp1}" "true")
file(WRITE "${REPO}/victim;extra.txt" "same bytes
"
)
file(APPEND "${REPO}/same|left.txt" "edited\n")
identity_field(TREE_STATE _tl)
repo_git(checkout -q -- "same|left.txt")
file(APPEND "${REPO}/same|right.txt" "edited\n")
identity_field(TREE_STATE _tr)
repo_git(checkout -q -- "same|right.txt")
expect_state(pipe-left-edit-counts "${_tl}")
expect_ne(pipe-paths-distinguish-edits "${_tr}" "${_tl}")
file(APPEND "${REPO}/uñicode.txt" "edited\n")
repo_git(config core.quotePath true)
identity_field(TREE_STATE _tq1)
repo_git(config core.quotePath false)
identity_field(TREE_STATE _tq2)
repo_git(config --unset core.quotePath)
repo_git(checkout -q -- "uñicode.txt")
expect_state(utf8-path-edit-counts "${_tq1}")
expect_eq(quotepath-does-not-move-state "${_tq2}" "${_tq1}")

# ------------------------------------------------------------------------------
# Object types: symlink text, executable bit, deletion, gitlink
# ------------------------------------------------------------------------------
file(WRITE "${REPO}/a.txt" "a\n")
file(WRITE "${REPO}/b.txt" "a.txt")
execute_process(COMMAND ln -s a.txt "${REPO}/link")
commit_all(symlink)
identity_field(TREE_STATE _ts0)
expect_eq(symlink-clean "${_ts0}" "clean")
file(REMOVE "${REPO}/link")
execute_process(COMMAND ln -s b.txt "${REPO}/link")
identity_field(TREE_STATE _ts1)
expect_state(symlink-retarget-counts "${_ts1}")
file(REMOVE "${REPO}/link")
execute_process(COMMAND ln -s a.txt "${REPO}/link")
identity_field(TREE_STATE _ts2)
expect_eq(symlink-restore-clean "${_ts2}" "clean")
execute_process(COMMAND chmod 0755 "${REPO}/a.txt")
identity_field(TREE_STATE _tm)
expect_state(mode-only-change-counts "${_tm}")
execute_process(COMMAND chmod 0644 "${REPO}/a.txt")
file(REMOVE "${REPO}/b.txt")
identity_field(TREE_STATE _tdel)
expect_state(deletion-counts "${_tdel}")
repo_git(checkout -q -- b.txt)
identity_field(TREE_STATE _tback)
expect_eq(types-restored-clean "${_tback}" "clean")

set(SUB "${WORK}/sub")
file(MAKE_DIRECTORY "${SUB}")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SUB}" init -q -b main . OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SUB}" config user.email fixture@example.invalid)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SUB}" config user.name fixture)
file(WRITE "${SUB}/s.txt" "sub\n")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${SUB}" -c user.email=fixture@example.invalid -c user.name=fixture
          -c commit.gpgsign=false add -A
  RESULT_VARIABLE _sub_step
  OUTPUT_QUIET ERROR_QUIET
)
if (NOT _sub_step EQUAL 0)
  message(FATAL_ERROR "fixture: git add -A in the submodule source failed")
endif ()
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${SUB}" -c user.email=fixture@example.invalid -c user.name=fixture
          -c commit.gpgsign=false commit -q -m sub1
  RESULT_VARIABLE _sub_step
  OUTPUT_QUIET ERROR_QUIET
)
if (NOT _sub_step EQUAL 0)
  message(FATAL_ERROR "fixture: git commit -q -m sub1 in the submodule source failed")
endif ()
repo_git(
  -c
  protocol.file.allow=always
  submodule
  add
  -q
  "${SUB}"
  sub
)
commit_all(gitlink)
identity_field(TREE_STATE _tg0)
expect_eq(gitlink-clean "${_tg0}" "clean")
file(WRITE "${REPO}/sub/s.txt" "sub2\n")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${REPO}/sub" -c user.email=fixture@example.invalid -c
          user.name=fixture -c commit.gpgsign=false commit -q -am sub2
  RESULT_VARIABLE _sub_commit
  OUTPUT_QUIET ERROR_QUIET
)
if (NOT _sub_commit EQUAL 0)
  message(FATAL_ERROR "fixture: the submodule commit failed")
endif ()
identity_field(TREE_STATE _tg1)
expect_state(gitlink-advance-counts "${_tg1}")

# ------------------------------------------------------------------------------
# Stat-only changes are not modifications; same-stat edits still are
# ------------------------------------------------------------------------------
repo_git(submodule -q update --checkout sub)
identity_field(TREE_STATE _tg2)
expect_eq(gitlink-restored-clean "${_tg2}" "clean")
file(TOUCH "${REPO}/text.txt")
identity_field(TREE_STATE _t6)
expect_eq(touch-is-not-dirty "${_t6}" "clean")
file(
  WRITE "${REPO}/text.txt"
  "ONE\ntwo\nthree\nfour\nfor deadbeef\nfor release-golden\nfor release-dirty\nfor release-1-gdeadbeef\n"
)
execute_process(COMMAND touch -r "${REPO}/blob.bin" "${REPO}/text.txt" OUTPUT_QUIET ERROR_QUIET)
identity_field(TREE_STATE _t7)
expect_state(same-size-edit-counts "${_t7}")
repo_git(checkout -q -- text.txt)

if (FAILURES GREATER 0)
  message(FATAL_ERROR "${FAILURES} check(s) failed")
endif ()
message(STATUS "all checks passed")
