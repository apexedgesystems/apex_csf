#!/usr/bin/env python3
"""Decide what a change has to run in CI, from the files it touches.

    git diff --name-only --no-renames origin/main...HEAD | ci-classify.py
    ci-classify.py --check tools/build/fixtures/ci-classify/cases.txt

Reads one changed path per line on stdin and prints one `name=true|false` line
per decision, the form a workflow appends to its outputs:

  cpp     the C++ build, its tests, the sanitizer and static-analysis jobs
  rust    the Rust tool tests, clippy and the spec/.auto pin
  py      the Python tool tests
  sh      the shell tool smoke
  docker  the gate images are rebuilt from the Dockerfiles, not pulled

A path selects the jobs of the first rule it matches (RULES, in order; `*` in
a pattern also crosses `/`, so demos/*/test covers a demo at any depth). The
rules scope DOWN only: a path no rule knows selects every job, so a new
top-level directory or a build or CI file can never silently run less. A
change set with no paths at all (a manual run on main) selects every job too,
and rebuilds nothing.

--check reads a table of cases, one per line as `<decisions> | <paths>` (the
decisions that must be true, then the changed paths, each list separated by
spaces; `-` for an empty list), and fails on the first case whose answer
differs. It is how `make check-ci-classify` holds the rules to their contract.

stdlib only: this runs on the bare CI host before any image exists.
"""

import fnmatch
import re
import sys

JOBS = ("cpp", "rust", "py", "sh")
EVERY_JOB = frozenset(JOBS)

# (patterns, jobs). A pattern is a shell glob in which `*` also crosses `/`.
RULES = (
    (("tools/Cargo.*", "tools/*/rust/*"), {"rust"}),
    # Scripts the build runs in place; nothing here has a test job of its own.
    (("tools/build/py/bin/*",), set()),
    (("tools/pyproject.toml", "tools/uv.lock", "tools/*/py/*"), {"py"}),
    # A demo's checkout scripts keep their pytest files beside them; make
    # test-py collects those directories with the members' own tests.
    (("demos/*/test/*.py",), {"py"}),
    (("tools/ops/sh/*",), {"sh"}),
    # Conformance contracts are multi-sided: every suite that pins one runs.
    (("compat/tprm/*",), {"cpp", "rust"}),
    # Component specs and their committed .auto headers are pinned against
    # each other by check-cdef, which runs in the rust job (it needs cdef_gen).
    (("*_data.toml", "*/.auto/*"), {"cpp", "rust"}),
    # A demo's Python scripts are tested by the Python job as well as built
    # around by the demo's C++ selection.
    (("demos/*/scripts/*.py",), {"cpp", "py"}),
    (("src/*", "demos/*", "apps/*", "cmake/*", "CMakeLists.txt", "CMakePresets.json"), {"cpp"}),
    (("*.md", "docs/*"), set()),
    # Two of the images under docker/dev are ones the gate jobs run in; a
    # change to either can alter every build.
    (("docker/dev/cpu.Dockerfile", "docker/dev/cuda.Dockerfile"), EVERY_JOB),
    # The tools packager has an executed regression in the shell suite.
    (("tools/build/sh/package-tools.sh",), {"sh"}),
    # The other images under docker/dev (cross and firmware) and the scripts
    # they copy are built after the merge; no gate job runs in them. Lint
    # still checks them.
    (("docker/dev/*", "tools/build/sh/*"), set()),
    # Everything else: the base image, compose files, workflows, make files,
    # root configuration, and whatever is new.
    (("*",), EVERY_JOB),
)

# What a gate image is built from: the Dockerfiles of the images the gate jobs
# run in, the compose file, and everything base.Dockerfile bakes (the locks
# and manifests, the dependency pins, the scripts it copies). Without a
# rebuild the gates would build offline against stale caches.
IMAGE_INPUTS = re.compile(
    r"^docker/base\.Dockerfile$|^docker/dev/cpu\.Dockerfile$|^docker/dev/cuda\.Dockerfile$"
    r"|^docker-compose\.yml$|^tools/build/sh/|^tools/build/py/bin/"
    r"|^tools/Cargo\.(lock|toml)$|^tools/[^/]+/rust/[^/]+/Cargo\.toml$"
    r"|^tools/(uv\.lock|pyproject\.toml)$|^tools/[^/]+/py/[^/]+/pyproject\.toml$"
    r"|^ExternalDependencies\.cmake$"
)


def classify(paths):
    """Return {decision: bool} for a list of changed paths."""
    jobs = set()
    for path in paths:
        for patterns, selected in RULES:
            if any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns):
                jobs |= selected
                break
    if not paths:
        jobs = set(EVERY_JOB)
    decisions = {job: job in jobs for job in JOBS}
    decisions["docker"] = any(IMAGE_INPUTS.search(path) for path in paths)
    return decisions


def check(table):
    """Run every case of a table; return the number that failed."""
    failed = cases = 0
    with open(table, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            expected, _, paths = (part.strip() for part in line.partition("|"))
            want = set() if expected == "-" else set(expected.split())
            paths = [] if paths == "-" else paths.split()
            got = {name for name, value in classify(paths).items() if value}
            cases += 1
            if got != want:
                failed += 1
                print(
                    "%s:%d: %s selects [%s], expected [%s]"
                    % (
                        table,
                        number,
                        " ".join(paths) or "(no paths)",
                        " ".join(sorted(got)),
                        " ".join(sorted(want)),
                    ),
                    file=sys.stderr,
                )
    print("[check-ci-classify] %d cases, %d failed" % (cases, failed))
    return failed


def main(argv):
    if len(argv) == 3 and argv[1] == "--check":
        return 1 if check(argv[2]) else 0
    if len(argv) != 1:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    paths = [line.strip() for line in sys.stdin if line.strip()]
    for name, value in classify(paths).items():
        print("%s=%s" % (name, "true" if value else "false"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
