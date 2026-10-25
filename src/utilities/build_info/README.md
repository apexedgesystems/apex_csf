# Apex Build Info Library

**Namespace:** `apex::build_info`
**Platform:** Hosted (Linux); skipped on bare-metal builds
**C++ Standard:** C++17
**Library:** `utilities_build_info`

The identity of the build a binary came from, as strings generated at build
time: `git describe`, commit, branch, tree state, build type, compiler and
target system. An executive logs the one-line summary at boot, so every run
log names the source state behind the binaries in its bank.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [What the Identity Carries](#2-what-the-identity-carries)
3. [How It Is Generated](#3-how-it-is-generated)
4. [Limits](#4-limits)
5. [RT-Safety](#5-rt-safety)
6. [Testing](#6-testing)

---

## 1. Quick Reference

```cpp
#include "src/utilities/build_info/inc/BuildInfo.hpp"

sysLog->info(label(), fmt::format("Build: {}", apex::build_info::summary()));
// Build: apex_csf v0.0.1-165-g7c9d744 (main) tree=clean Release Clang-21.1.8 Linux/x86_64
```

| Accessor         | Example                   | Source                                       |
| ---------------- | ------------------------- | -------------------------------------------- |
| `summary()`      | the line above            | every field below, in that order             |
| `project()`      | `apex_csf`                | CMake project name                           |
| `version()`      | `0.0.1`                   | CMake project version                        |
| `describe()`     | `v0.0.1-165-g7c9d744`     | `git describe --tags --always --dirty`       |
| `commit()`       | `7c9d744a07c7`            | `git rev-parse --short=12 HEAD`              |
| `branch()`       | `main`, `HEAD` (detached) | `git rev-parse --abbrev-ref HEAD`            |
| `treeState()`    | `clean`, `3f1c0a9b7d2e`   | SHA-256 of `git diff HEAD`, first twelve hex |
| `dirty()`        | `false`                   | `treeState()` names a diff                   |
| `buildType()`    | `Release`                 | the configuration built                      |
| `compiler()`     | `Clang-21.1.8`            | CMake compiler id and version                |
| `targetSystem()` | `Linux/x86_64`            | CMake system name and processor              |

Link `utilities_build_info`; every accessor returns a pointer to static storage.

## 2. What the Identity Carries

The string is a function of two things only: the source state (commit, and
the exact modifications on top of it) and the configuration (build type,
compiler, target). It carries no build time, so two builds of the same tree
and configuration produce the same identity, and a reproduced build can be
checked against a run log by eye.

Tree state is the strongest field. `git describe --dirty` says that tracked
files differ from HEAD; `treeState()` says which modifications, as a hash of
the diff (staged and unstaged alike, the same set `--dirty` inspects). Two
checkouts carrying the same patch report the same state.

## 3. How It Is Generated

`cmake/GenerateBuildInfo.cmake` runs in CMake script mode from a custom
target with no inputs, so it executes on every build that needs the library.
It reads git, fills `src/BuildInfo.cpp.in`, and replaces the generated
`BuildInfo.cpp` only when the content differs: a build with no identity change
recompiles and relinks nothing, and an edit anywhere in the tree recompiles
one translation unit and relinks this one small library.

The library is SHARED like the other hosted libraries. The identity lives in
`libutilities_build_info.so` beside its siblings in a deployment bank, and
the executive library that logs it does not relink on every edit.

## 4. Limits

- Untracked files are not part of the tree state; a new file that is not yet
  added to git leaves the identity unchanged.
- Without git or without the repository metadata (a source archive, an image
  build context that omits `.git`) the git-derived fields read `unknown`.
  The configuration fields are always present.
- The identity names the source state the library was generated from. It
  cannot vouch for every object in a build tree; the build system does that,
  and the build documentation lists the cases where it cannot.

## 5. RT-Safety

All accessors are `noexcept`, allocation-free and marked `APEX_NONBLOCKING`:
they return pointers into static storage initialized at load time.

## 6. Testing

`TestUtilitiesBuildInfo` (utst/) checks the shape of every field and their
mutual consistency for the build it was compiled in: the commit is twelve hex
digits or `unknown`, `dirty()` agrees with `treeState()` and with the `-dirty`
suffix of `describe()`, the summary carries every field.
