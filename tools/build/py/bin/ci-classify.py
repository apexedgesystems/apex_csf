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

The ops and the analysis role each run their suites together (ROLE_SUITES): a
path under one of them that selects any job selects the role's other suites
as well. So an ops crate, script or package runs the ops Rust tests, the
shell smoke and the Python suite, which holds the target-generation tests;
none of them selects what stands on the build role.

What an image is built from is read from its Dockerfile, not listed here: the
Dockerfile itself and every file a COPY or ADD takes from the repository. In
docker/base.Dockerfile those copies are all in the `bakes` stage, which is
what separates a bake from `images`. One kind of copied file is not an
input: a Cargo.toml. cargo needs every member manifest to read the workspace,
but what it fetches is what Cargo.lock lists; a manifest edit that changed
that would change the lock.

A source is read in the shell form or the JSON form, with a `*` or `?` in a
path segment. An input written any other way (a build variable, a quoted
word, a character class, a here-document, a RUN that mounts the repository)
is refused, and so is a whole file whose `# escape=` directive changes the
continuation character: the run ends nonzero with the file and line, so the
input is rewritten where it was added instead of going unseen by every later
change.

Who rebuilds for a changed bake follows from the jobs its own rule selects
(not the rest of its role's suites, which do not install from it): the
Rust tests when it selects a Rust role, the Python tests when it selects py,
and the C++ jobs and the dictionary gate when it selects cpp or rust_build
(they compile the build role's crates, and cargo resolves the whole lock to
do it). So the Rust lock rebuilds everything that compiles a crate, and the
Python lock rebuilds the Python job alone. A bake whose path selects none of
those (no job, or only the shell smoke, which runs on the bare host) has no
known consumer, and rebuilds for all of them. Lint uses no bake and rebuilds
for `images` only.

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
alone returns to the real ones. A line `@refused <Dockerfile>...` is a case
per COPY, ADD, RUN and ONBUILD of the named files, and per escape directive
that is not read, each of which must be refused. It
then reads every Dockerfile under docker/, so an input the reader refuses
fails in the pull request that writes it. It is how `make check-ci-classify`
holds the rules to their contract.

stdlib only: this runs on the bare CI host before any image exists.
"""

import fnmatch
import glob
import json
import os
import posixpath
import re
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

# The roles whose suites run together: under the pattern, a path that
# selects any job selects these too.
ROLE_SUITES = (
    ("tools/ops/*", frozenset({"rust_ops", "py", "sh"})),
    ("tools/analysis/*", frozenset({"rust_analysis", "py"})),
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

# A parser directive: `# key=value`, spaces allowed around each part.
PARSER_DIRECTIVE = re.compile(r"\s*#\s*([A-Za-z][A-Za-z0-9]*)\s*=\s*(.*?)\s*$")
# An instruction's leading --flags, then its operands.
FLAGS_THEN_REST = re.compile(r"((?:--\S+(?:\s+|$))*)(.*)", re.DOTALL)
# What repository_sources does not read in a source: a build variable, shell
# quoting, a character class, an escape, a here-document.
UNREAD_IN_SOURCE = re.compile(r"""[$"'\[\\<]""")
# An ADD source fetched from elsewhere.
REMOTE_SOURCE = re.compile(r"[A-Za-z][A-Za-z0-9+.-]*://|git@")


def instructions(dockerfile):
    """Yield (line number, keyword, text after it) for each instruction of a Dockerfile.

    Comment lines are dropped and a trailing backslash joins the next line,
    the order docker applies them in. The backslash is docker's default: a
    file whose leading `# escape=` directive names another character yields
    that directive alone, as ESCAPE, since nothing after it can be read by
    these rules.
    """
    with open(dockerfile, encoding="utf-8") as handle:
        lines = handle.read().split("\n")
    # Parser directives are the `# key=value` lines before anything else.
    for number, line in enumerate(lines, 1):
        directive = PARSER_DIRECTIVE.match(line)
        if not directive:
            break
        if directive.group(1).lower() == "escape" and directive.group(2) != "\\":
            yield number, "ESCAPE", directive.group(2)
            return
    first, joined = 0, ""
    for number, line in enumerate(lines, 1):
        if line.lstrip().startswith("#"):
            continue
        first = first if joined else number
        line = line.rstrip()
        if line.endswith("\\"):
            joined += line[:-1] + " "
            continue
        words = (joined + line).split(None, 1)
        joined = ""
        if words:
            yield first, words[0].upper(), words[1] if len(words) > 1 else ""


def repository_sources(keyword, rest):
    """Return the repository paths one instruction takes into the image.

    COPY and ADD, alone or after ONBUILD, in the shell form or the JSON form;
    a path is normalized (`./a/` is `a`) and may hold `*` and `?`. A --from
    takes from another
    image or stage and an ADD from a URL, neither from the repository. Raises
    ValueError for an input written in a form this does not read, so that it
    is refused where it is written instead of going unseen.
    """
    if keyword == "ESCAPE":
        raise ValueError(
            "the escape directive sets %s; lines are joined on a backslash only, "
            "so the file's inputs cannot be read" % rest
        )
    if keyword == "ONBUILD":
        # Deferred to the images built on this one, which are built from the
        # same repository and rebuilt when this one is.
        deferred = rest.split(None, 1)
        return repository_sources(deferred[0].upper(), deferred[1]) if len(deferred) == 2 else []
    flags, rest = FLAGS_THEN_REST.match(rest).groups()
    flags = flags.split()
    if keyword == "RUN":
        for flag in flags:
            if not flag.startswith("--mount="):
                continue
            options = dict(option.partition("=")[::2] for option in flag[8:].split(","))
            if options.get("type", "bind") == "bind" and "from" not in options:
                raise ValueError("RUN %s mounts the repository, an input this does not read" % flag)
        return []
    if keyword not in ("COPY", "ADD") or any(flag.startswith("--from") for flag in flags):
        return []
    operands = None
    if rest.startswith("["):
        try:
            operands = json.loads(rest)
        except ValueError:
            pass
    # What is not a JSON list of strings docker reads as the shell form.
    if not isinstance(operands, list) or not all(isinstance(word, str) for word in operands):
        operands = rest.split()
    sources = []
    for source in operands[:-1]:
        if keyword == "ADD" and REMOTE_SOURCE.match(source):
            continue
        path = posixpath.normpath(source).lstrip("/")
        if UNREAD_IN_SOURCE.search(source) or path.split("/")[0] == "..":
            raise ValueError(
                "%s source %s is not read (a variable, a quoted word, a character class, a "
                "here-document or a path outside the repository); write the path out"
                % (keyword, source)
            )
        sources.append(path)
    return sources


def copied_sources(dockerfile):
    """Return [(stage, path)] for every file a Dockerfile takes from the repository.

    A stage is named by its FROM ... AS name. An input in a form
    repository_sources refuses ends the run with the file and line.
    """
    stage = ""
    found = []
    for number, keyword, rest in instructions(dockerfile):
        if keyword == "FROM":
            words = rest.split()
            stage = words[-1] if len(words) >= 3 and words[-2].upper() == "AS" else ""
            continue
        try:
            found.extend((stage, source) for source in repository_sources(keyword, rest))
        except ValueError as refusal:
            raise SystemExit("%s:%d: %s" % (dockerfile, number, refusal)) from None
    return found


def decides_image(source):
    """Whether a copied file decides what the image holds (see the module text)."""
    return os.path.basename(source) != "Cargo.toml"


def touches(paths, source):
    """Whether a change set touches a copied source.

    A source is a file, a directory (its content) or the whole repository
    (`.`); a `*` or `?` matches within one path segment, as docker's does.
    """
    wanted = [] if source == "." else source.split("/")
    return any(
        len(have) >= len(wanted) and all(map(fnmatch.fnmatchcase, have, wanted))
        for have in (path.split("/") for path in paths)
    )


def selected_jobs(path):
    """Return the jobs of the first rule a path matches (the last matches any).

    These are the jobs that build or install the path, which is what decides
    who rebuilds for a bake and which crates a crate's users are.
    """
    return next(
        selected
        for patterns, selected in RULES
        if any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)
    )


def jobs_to_run(path):
    """Return the jobs a changed path runs: its rule's, and its role's other suites."""
    jobs = set(selected_jobs(path))
    for pattern, suites in ROLE_SUITES:
        if jobs and fnmatch.fnmatchcase(path, pattern):
            jobs |= suites
    return jobs


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
                baked |= (selected_jobs(path) - {"sh"}) or EVERY_JOB
    return images, baked


def image_dockerfiles():
    """Return every Dockerfile under docker/."""
    return sorted(glob.glob("docker/**/*.Dockerfile", recursive=True))


def dockerfiles_to_rebuild(paths):
    """Return the Dockerfiles under docker/ a change set rebuilds directly.

    A Dockerfile that copies the whole repository (the release builders) is
    named only when it changed itself: such an image is built from the tree
    when it is needed, not kept current with it.
    """
    rebuilt = []
    for dockerfile in image_dockerfiles():
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
        jobs |= jobs_to_run(path)
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
    # An input the reader refuses ends the run here, with its file and line.
    for dockerfile in image_dockerfiles():
        copied_sources(dockerfile)
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
            if line.startswith("@refused"):
                for dockerfile in line.split()[1:]:
                    for at, keyword, rest in instructions(dockerfile):
                        if keyword not in ("COPY", "ADD", "RUN", "ONBUILD", "ESCAPE"):
                            continue
                        cases += 1
                        try:
                            read = repository_sources(keyword, rest)
                        except ValueError:
                            continue
                        failed += 1
                        print(
                            "%s:%d: %s %s is read as [%s], expected a refusal"
                            % (dockerfile, at, keyword, rest, " ".join(read)),
                            file=sys.stderr,
                        )
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
    print("[check-ci-classify] %d Dockerfiles read" % len(image_dockerfiles()))
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
