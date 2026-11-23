#!/usr/bin/env python3
"""Decide what a change has to run in CI, from the files it touches.

    git diff --name-only --no-renames origin/main...HEAD | ci-classify.py
    git diff --name-only <range> | ci-classify.py --dockerfiles
    ci-classify.py --check tools/build/fixtures/ci-classify/cases.txt
    (cd tools && cargo metadata --format-version 1 --no-deps) | ci-classify.py --check-crates

Reads one changed path per line on stdin and prints one `name=true|false` line
per decision, the form a workflow appends to its outputs:

  cpp            the C++ build, its tests, the sanitizer and static-analysis
                 jobs
  rust_build     the build role's Rust tests and clippy, the spec/.auto pin
                 and the dictionary gate: what the C++ build stands on
  rust_ops       the ops role's Rust tests and clippy
  rust_analysis  the analysis role's Rust tests and clippy
  py             the Python tool tests
  sh             the shell tool smoke
  images         an image a job runs in is defined differently, or built
                 differently: every job rebuilds its image from the
                 Dockerfiles instead of pulling it
  bakes_gate     the dependencies baked into those images differ for the C++
                 jobs and the dictionary gate, which rebuild
  bakes_rust     the same for the Rust tool tests
  bakes_py       the same for the Python tool tests

and one line `rust_roles=<roles>`, the roles of the selected rust_* decisions
separated by spaces, the form `make test-rust ROLES=...` takes.

A path selects the jobs of the first rule it matches (RULES, in order; `*` in
a pattern also crosses `/`, so demos/*/test covers a demo at any depth). The
rules scope DOWN only: a path no rule knows selects every job, so a new
top-level directory or a build or CI file can never silently run less. A
change set with no paths at all (a manual run on main) selects every job too,
and rebuilds nothing.

What an image is built from is read from its Dockerfile, not listed here: the
Dockerfile itself and every file a COPY takes from the repository. In
docker/base.Dockerfile those copies are all in the `bakes` stage, which is
what separates a bake from `images`. One kind of copied file is not an
input: a Cargo.toml. cargo needs every member manifest to read the workspace,
but what it fetches is what Cargo.lock lists; a manifest edit that changed
that would change the lock.

Who rebuilds for a changed bake follows from the jobs its path selects: the
Rust tests when it selects a Rust role, the Python tests when it selects py,
and the C++ jobs and the dictionary gate when it selects cpp or rust_build
(they compile the build role's crates, and cargo resolves the whole lock to
do it). So the Rust lock rebuilds everything that compiles a crate, and the
Python lock rebuilds the Python job alone. A bake whose path selects no job
has no known consumer, and rebuilds for all of them. Lint uses no bake and
rebuilds for `images` only.

--check-crates reads `cargo metadata` for the tools workspace on stdin and
fails when a crate is linked by a crate of a role its own paths do not
select: the role rules name directories, and this holds them to the crate
graph cargo reports.

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
import json
import os
import shlex
import sys

RUST_JOBS = ("rust_build", "rust_ops", "rust_analysis")
JOBS = ("cpp",) + RUST_JOBS + ("py", "sh")
EVERY_JOB = frozenset(JOBS)

# (patterns, jobs). A pattern is a shell glob in which `*` also crosses `/`.
RULES = (
    # The workspace manifest and the lock are every crate's.
    (("tools/Cargo.*",), set(RUST_JOBS)),
    # The TPRM library is linked by the ops crate as well as the build one.
    (("tools/build/rust/apex-tprm/*",), {"rust_build", "rust_ops"}),
    # The build role: its crates, the proto profile cdef_gen reads, and the
    # fixtures of the dictionary gate.
    (
        ("tools/build/rust/*", "tools/build/proto/*", "tools/build/fixtures/check-data-db/*"),
        {"rust_build"},
    ),
    (("tools/ops/rust/*",), {"rust_ops"}),
    (("tools/analysis/rust/*",), {"rust_analysis"}),
    # Scripts the build and CI run in place, and this classifier's own cases;
    # lint checks them and nothing here has a test job of its own.
    (("tools/build/py/bin/*", "tools/build/fixtures/ci-classify/*"), set()),
    (("tools/pyproject.toml", "tools/uv.lock", "tools/*/py/*"), {"py"}),
    # A demo's checkout scripts keep their pytest files beside them; make
    # test-py collects those directories with the members' own tests.
    (("demos/*/test/*.py",), {"py"}),
    (("tools/ops/sh/*",), {"sh"}),
    # A C++ tool of any role is a target of the C++ build.
    (("tools/*/cpp/*",), {"cpp"}),
    # The TPRM conformance vectors are pinned from both sides: the C++ loader's
    # tests and the TPRM library's.
    (("compat/tprm/*",), {"cpp", "rust_build"}),
    # Component specs and their committed .auto headers are pinned against
    # each other by check-cdef, and the dictionary gate reads both.
    (("*_data.toml", "*/.auto/*"), {"cpp", "rust_build"}),
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
    # root configuration, a tools role or language no rule names, and
    # whatever is new.
    (("*",), EVERY_JOB),
)

# The images the gate jobs run in.
GATE_DOCKERFILES = (
    "docker/base.Dockerfile",
    "docker/dev/cpu.Dockerfile",
    "docker/dev/cuda.Dockerfile",
)
# What a rebuild of those images reads besides the Dockerfiles: the compose
# file with every build's arguments, the overlay naming the registry cache a
# rebuild starts from, and the action that pulls or rebuilds a job's image.
# Only a rebuild runs what a change to one of them changed.
IMAGE_BUILD_FILES = (
    "docker-compose.yml",
    "docker-compose.ci-cache.yml",
    ".github/actions/dev-setup/action.yml",
)
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


def selected_jobs(path):
    """Return the jobs of the first rule a path matches (the last matches any)."""
    return next(
        selected
        for patterns, selected in RULES
        if any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)
    )


def image_decisions(paths, gates=GATE_DOCKERFILES):
    """Return (images, jobs) for a list of changed paths.

    `jobs` is the set of jobs whose baked dependencies changed: those of every
    changed path the bakes stage copies (see the module text).
    """
    images = any(path in IMAGE_BUILD_FILES for path in paths)
    baked = set()
    for dockerfile in gates:
        images = images or dockerfile in paths
        for stage, source in copied_sources(dockerfile):
            if not decides_image(source):
                continue
            touching = [path for path in paths if touches([path], source)]
            if stage != BAKES_STAGE:
                images = images or bool(touching)
                continue
            for path in touching:
                baked |= selected_jobs(path) or EVERY_JOB
    return images, baked


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
        jobs |= selected_jobs(path)
    if not paths:
        jobs = set(EVERY_JOB)
    decisions = {job: job in jobs for job in JOBS}
    decisions["images"], baked = image_decisions(paths, gates)
    decisions["bakes_gate"] = bool(baked & {"cpp", "rust_build"})
    decisions["bakes_rust"] = bool(baked & set(RUST_JOBS))
    decisions["bakes_py"] = "py" in baked
    return decisions


def rust_roles(decisions):
    """Return the selected Rust roles, as `make test-rust ROLES=...` takes them."""
    return " ".join(job[len("rust_") :] for job in RUST_JOBS if decisions[job])


def check_crates(metadata):
    """Hold the role rules to the crate graph; return the number of crates missed.

    `metadata` is `cargo metadata --no-deps` for the tools workspace, run from
    tools/. A crate's own job is the one its directory selects; every crate
    that links it, directly or through another, must have its job selected by
    the same path.
    """
    tools = metadata["workspace_root"]
    crates = {}
    for package in metadata["packages"]:
        directory = os.path.dirname(package["manifest_path"])
        links = [d["path"] for d in package["dependencies"] if d.get("path")]
        crates[directory] = (package["name"], links)

    def path_in_repository(directory):
        return "tools/%s/src/lib.rs" % os.path.relpath(directory, tools)

    def users(directory, seen):
        for other, (_, links) in crates.items():
            if directory in links and other not in seen:
                seen.add(other)
                users(other, seen)
        return seen

    missed = 0
    for directory, (name, _) in sorted(crates.items()):
        selected = selected_jobs(path_in_repository(directory))
        for user in sorted(users(directory, set())):
            needed = selected_jobs(path_in_repository(user)) & set(RUST_JOBS)
            if not needed <= selected:
                missed += 1
                print(
                    "%s is linked by %s, whose tests (%s) a change to %s does not select"
                    % (
                        name,
                        crates[user][0],
                        " ".join(sorted(needed - selected)),
                        os.path.relpath(directory, os.path.dirname(tools)),
                    ),
                    file=sys.stderr,
                )
    print("[check-ci-crates] %d crates, %d missed" % (len(crates), missed))
    return missed


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
    if argv[1:] == ["--check-crates"]:
        return 1 if check_crates(json.load(sys.stdin)) else 0
    if argv[1:] == ["--dockerfiles"]:
        paths = [line.strip() for line in sys.stdin if line.strip()]
        for dockerfile in dockerfiles_to_rebuild(paths):
            print(dockerfile)
        return 0
    if len(argv) != 1:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    paths = [line.strip() for line in sys.stdin if line.strip()]
    decisions = classify(paths)
    for name, value in decisions.items():
        print("%s=%s" % (name, "true" if value else "false"))
    print("rust_roles=%s" % rust_roles(decisions))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
