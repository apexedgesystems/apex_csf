#!/usr/bin/env python3
"""Decide what a change has to run in CI, from the files it touches.

    git diff --name-only --no-renames origin/main...HEAD | ci-classify.py
    git diff --name-only <range> | ci-classify.py --dockerfiles
    ci-classify.py --check tools/build/fixtures/ci-classify/cases.txt

Reads one changed path per line on stdin and prints one `name=true|false` line
per decision, the form a workflow appends to its outputs:

  cpp     the C++ build, its tests, the sanitizer and static-analysis jobs
  rust    the Rust tool tests, clippy and the spec/.auto pin
  py      the Python tool tests
  sh      the shell tool smoke
  images  an image a gate job runs in is defined differently: every job
          rebuilds its image from the Dockerfiles instead of pulling it
  bakes   the dependencies baked into those images differ: the jobs that
          build with them rebuild; lint, which uses none of them, pulls

A path selects the jobs of the first rule it matches (RULES, in order; `*` in
a pattern also crosses `/`, so demos/*/test covers a demo at any depth). The
rules scope DOWN only: a path no rule knows selects every job, so a new
top-level directory or a build or CI file can never silently run less. A
change set with no paths at all (a manual run on main) selects every job too,
and rebuilds nothing.

What an image is built from is read from its Dockerfile, not listed here: the
Dockerfile itself and every file a COPY takes from the repository. In
docker/base.Dockerfile those copies are all in the `bakes` stage, which is
what separates `bakes` from `images`. One kind of copied file is not an
input: a Cargo.toml. cargo needs every member manifest to read the workspace,
but what it fetches is what Cargo.lock lists; a manifest edit that changed
that would change the lock.

--dockerfiles prints, for the changed paths on stdin, each Dockerfile under
docker/ that is one of them or copies one of them: the images the publish
workflow has to rebuild before it follows the graph down to their children.

--check reads a table of cases, one per line as `<decisions> | <paths>` (the
decisions that must be true, then the changed paths, each list separated by
spaces; `-` for an empty list), and reports every case whose answer differs.
A line `@gates <Dockerfile>...` makes the named files the gate images for the
cases after it (a fixture Dockerfile, to pin how copies are read); `@gates`
alone returns to the real ones. It is how `make check-ci-classify` holds the
rules to their contract.

stdlib only: this runs on the bare CI host before any image exists.
"""

import fnmatch
import glob
import os
import shlex
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
    # The script that bakes the FetchContent dependencies, and their crates and
    # wheels, into the images: every build stands on what it leaves.
    (("tools/build/sh/bake-external-deps.sh",), EVERY_JOB),
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

# The images the gate jobs run in, and the compose file every image build
# reads its arguments from.
GATE_DOCKERFILES = (
    "docker/base.Dockerfile",
    "docker/dev/cpu.Dockerfile",
    "docker/dev/cuda.Dockerfile",
)
COMPOSE_FILE = "docker-compose.yml"
BAKES_STAGE = "bakes"


def copied_sources(dockerfile):
    """Return [(stage, path)] for every file a Dockerfile copies from the repository.

    A COPY with --from takes from another image or stage, not from the
    repository, and is left out. A stage is named by its FROM ... AS name.
    """
    with open(dockerfile, encoding="utf-8") as handle:
        text = handle.read().replace("\\\n", " ")
    stage = ""
    found = []
    for line in text.splitlines():
        if not line.lstrip().upper().startswith(("FROM ", "COPY ")):
            continue
        words = shlex.split(line, comments=True)
        if words[0].upper() == "FROM":
            stage = words[-1] if len(words) >= 4 and words[-2].upper() == "AS" else ""
            continue
        flags = [word for word in words[1:] if word.startswith("--")]
        operands = [word for word in words[1:] if not word.startswith("--")]
        if not any(flag.startswith("--from") for flag in flags):
            found.extend((stage, source) for source in operands[:-1])
    return found


def decides_image(source):
    """Whether a copied file decides what the image holds (see the module text)."""
    return os.path.basename(source) != "Cargo.toml"


def touches(paths, source):
    """Whether a change set touches a copied source (a file, or a directory's content)."""
    source = source.rstrip("/")
    return any(path == source or path.startswith(source + "/") for path in paths)


def image_decisions(paths, gates=GATE_DOCKERFILES):
    """Return (images, bakes) for a list of changed paths."""
    images = COMPOSE_FILE in paths
    bakes = False
    for dockerfile in gates:
        images = images or dockerfile in paths
        for stage, source in copied_sources(dockerfile):
            if decides_image(source) and touches(paths, source):
                if stage == BAKES_STAGE:
                    bakes = True
                else:
                    images = True
    return images, bakes


def dockerfiles_to_rebuild(paths):
    """Return the Dockerfiles under docker/ a change set rebuilds directly.

    A Dockerfile that copies the whole repository (the release builders) is
    named only when it changed itself: such an image is built from the tree
    when it is needed, not kept current with it.
    """
    rebuilt = []
    for dockerfile in sorted(glob.glob("docker/**/*.Dockerfile", recursive=True)):
        if dockerfile in paths or any(
            source != "." and decides_image(source) and touches(paths, source)
            for _, source in copied_sources(dockerfile)
        ):
            rebuilt.append(dockerfile)
    return rebuilt


def classify(paths, gates=GATE_DOCKERFILES):
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
    decisions["images"], decisions["bakes"] = image_decisions(paths, gates)
    return decisions


def check(table):
    """Run every case of a table; return the number that failed."""
    failed = cases = 0
    gates = GATE_DOCKERFILES
    with open(table, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("@gates"):
                gates = tuple(line.split()[1:]) or GATE_DOCKERFILES
                continue
            expected, _, paths = (part.strip() for part in line.partition("|"))
            want = set() if expected == "-" else set(expected.split())
            paths = [] if paths == "-" else paths.split()
            got = {name for name, value in classify(paths, gates).items() if value}
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
    if argv[1:] == ["--dockerfiles"]:
        paths = [line.strip() for line in sys.stdin if line.strip()]
        for dockerfile in dockerfiles_to_rebuild(paths):
            print(dockerfile)
        return 0
    if len(argv) != 1:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    paths = [line.strip() for line in sys.stdin if line.strip()]
    for name, value in classify(paths).items():
        print("%s=%s" % (name, "true" if value else "false"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
