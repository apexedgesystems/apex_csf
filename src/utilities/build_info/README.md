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

| Accessor         | Example                    | Source                                                                            |
| ---------------- | -------------------------- | --------------------------------------------------------------------------------- |
| `summary()`      | the line above             | project, describe, branch, tree state, build type, compiler, target               |
| `project()`      | `apex_csf`                 | CMake project name                                                                |
| `version()`      | `0.0.1`                    | CMake project version                                                             |
| `describe()`     | `v0.0.1-165-g7c9d744a07c7` | `git describe --tags --always --abbrev=12`, `-dirty` appended from the tree state |
| `commit()`       | `7c9d744a07c7`             | `git rev-parse --short=12 HEAD`                                                   |
| `branch()`       | `main`, `HEAD` (detached)  | `git rev-parse --abbrev-ref HEAD`                                                 |
| `treeState()`    | `clean`, `3f1c0a9b7d2e`    | the working tree's git tree id, first twelve hex; see 2                           |
| `dirty()`        | `false`                    | `treeState()` is not `clean`                                                      |
| `buildType()`    | `Release`                  | the configuration built                                                           |
| `compiler()`     | `Clang-21.1.8`             | CMake compiler id and version                                                     |
| `targetSystem()` | `Linux/x86_64`             | CMake system name and processor                                                   |

Link `utilities_build_info`. The string accessors return pointers to static
storage; `dirty()` returns a bool.

## 2. What the Identity Carries

The string is a function of two things only: the source state (commit, and
the exact modifications on top of it) and the configuration (build type,
compiler, target). It carries no build time, so two builds of the same tree
and configuration produce the same identity, and a reproduced build can be
checked against a run log by eye.

Tree state is the strongest field. It is the id of the tree git would commit
from the working tree's tracked paths, built in a temporary index
(`read-tree HEAD`, `add -u`, `write-tree`) so the checkout's own index is
neither read nor written: every path whatever its name, every mode and object
type (regular file, symlink, gitlink), every deletion, under git's own content
normalization (clean filters, line endings), the content a commit would
record. `clean` means that tree is HEAD's. Diff settings, path quoting and
textconv drivers play no part, and the index's stat cache is not consulted, so
two checkouts carrying the same modifications report the same state, two
different modifications differ, and an edit that keeps a file's size and
timestamps still counts. Untracked files are not part of it. The `-dirty`
mark on the description is appended from this state, so `describe()` and
`dirty()` agree by construction; the description pins twelve commit digits
(`--abbrev=12`) so it reads the same under any `core.abbrev`.

## 3. How It Is Generated

`apex_add_build_info` (cmake/apex/BuildInfo.cmake) gives the library a
refresh target with no inputs, so the generator, the same module run in
CMake script mode, executes on every build that needs the library. It
reads git, fills `src/BuildInfo.cpp.in`, and replaces the generated
`BuildInfo.cpp` only when the content differs. A build with no identity
change adds no identity-induced compile or link work (unrelated rebuilds
proceed as they would anyway). A change in the identity recompiles
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
the string accessors return pointers into static storage initialized at load
time, and `dirty()` returns a bool.

## 6. Testing

`TestUtilitiesBuildInfo` (utst/) checks the shape of every field and their
mutual consistency for the build it was compiled in: the commit is twelve hex
digits or `unknown`, `dirty()` agrees with `treeState()` and with the `-dirty`
suffix of `describe()` whenever the tree is dirty, the summary carries its
fields. The generator's contract is checked from the outside by the fixture
in `cmake/apex/test/build_info/`, which builds its own repositories: the
description forms (exact tags, including names that look like a hash, a hash
suffix or a dirty mark; tag-N-g<12 digits> under a short `core.abbrev`), and
the tree state unmoved by diff context, prefixes and path quoting, counting
edits a textconv driver would hide, paths with `;` and `|` and UTF-8 names,
symlink retargets, mode-only changes, deletions, gitlink advances and
same-size same-timestamp edits, and treating a touched file as clean.

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
