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
7. [See Also](#7-see-also)

---

## 1. Quick Reference

```cpp
#include "src/utilities/build_info/inc/BuildInfo.hpp"

sysLog->info(label(), fmt::format("Build: {}", apex::build_info::summary()));
// Build: apex_csf v0.0.1-165-g7c9d744 (main) tree=clean Release Clang-21.1.8 Linux/x86_64
```

| Accessor         | Example                    | Source                                                              |
| ---------------- | -------------------------- | ------------------------------------------------------------------- |
| `summary()`      | the line above             | project, describe, branch, tree state, build type, compiler, target |
| `project()`      | `apex_csf`                 | CMake project name                                                  |
| `version()`      | `0.0.1`                    | CMake project version                                               |
| `describe()`     | `v0.0.1-165-g7c9d744a07c7` | `git describe --tags --always --dirty --abbrev=12`                  |
| `commit()`       | `7c9d744a07c7`             | `git rev-parse --short=12 HEAD`                                     |
| `branch()`       | `main`, `HEAD` (detached)  | `git rev-parse --abbrev-ref HEAD`                                   |
| `treeState()`    | `clean`, `3f1c0a9b7d2e`    | first twelve hex of the SHA-256 of the tracked modifications, see 2 |
| `dirty()`        | `false`                    | `treeState()` names modifications                                   |
| `buildType()`    | `Release`                  | the configuration built                                             |
| `compiler()`     | `Clang-21.1.8`             | CMake compiler id and version                                       |
| `targetSystem()` | `Linux/x86_64`             | CMake system name and processor                                     |

Link `utilities_build_info`. The string accessors return pointers to static
storage; `dirty()` returns a bool.

## 2. What the Identity Carries

The string is a function of two things only: the source state (commit, and
the exact modifications on top of it) and the configuration (build type,
compiler, target). It carries no build time, so two builds of the same tree
and configuration produce the same identity, and a reproduced build can be
checked against a run log by eye.

Tree state is the strongest field. `git describe --dirty` says that tracked
files differ from HEAD; `treeState()` says which modifications: every tracked
path whose content or mode differs from HEAD, staged or unstaged alike (the
set `--dirty` inspects), listed with the raw hash of its working-tree bytes,
the listing hashed. It is built from git plumbing (`diff-index`,
`hash-object --no-filters`) rather than from diff text, so diff settings such
as context or prefixes do not move it, and a textconv driver cannot hide an
edit. Two checkouts carrying the same patch report the same state. The
description pins twelve commit digits (`--abbrev=12`) so it reads the same
under any `core.abbrev`.

## 3. How It Is Generated

`apex_add_build_info` (cmake/apex/BuildInfo.cmake) gives the library a
refresh target with no inputs, so the generator, the same module run in
CMake script mode, executes on every build that needs the library. It
reads git, fills `src/BuildInfo.cpp.in`, and replaces the generated
`BuildInfo.cpp` only when the content differs. A build with no identity
change recompiles and relinks nothing. A change in the identity recompiles
this one translation unit and relinks this library, and since consumers hold
a file-level dependency on the shared libraries they link, the executive
library and the executables that link it relink as well: on a Debug tree
that is a 4 ms compile and about 50 ms per link, under a second of link work
spread over the job slots.

The library is SHARED like the other hosted libraries, so the identity lives
in `libutilities_build_info.so` beside its siblings in a deployment bank.

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
suffix of `describe()`, the description takes one of its documented forms,
the summary carries its fields. The generator's contract (tree state follows
tracked content, not diff presentation; description forms) is checked from
the outside by the fixture in `cmake/apex/test/build_info/`, which builds its
own repositories.

```bash
docker compose run --rm -T dev ctest --preset hosted-x86_64-debug -L build_info
docker compose run --rm -T dev make check-build-info
```

## 7. See Also

- [cmake/apex/README.md](../../../cmake/apex/README.md): the `BuildInfo.cmake`
  module and `apex_add_build_info`.
- [src/system/core/executive/README.md](../../system/core/executive/README.md):
  the `Build:` line in the executive's boot log.
- [mk/README.md](../../../mk/README.md): the incremental-build cases the build
  system cannot see, which the identity helps a reader check.
- [src/utilities/compatibility/README.md](../compatibility/README.md): the RT
  attribute the accessors carry.
