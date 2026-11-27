#!/usr/bin/env python3
"""Stage a bundle's tools so the package runs from wherever it is copied.

A bundle is the user's filesystem for one target: it is copied onto the target
and run in place, with nothing installed, built or fetched afterwards. The C++
tools arrive through CMake's install rules; this script stages the kinds CMake
has no install rule for and checks every staged tool against the package's
architecture.

    stage_bundle_tools.py --bundle <name> --dest <bundle>/tools
                          --reference <executive>
                          [--tool <kind>:<name>:<file>[:<python-lib>]]...
                          [--requires <name>=<command>[: <use>]]...

Kinds:
  bin     a prebuilt executable (a Rust tool).
  script  a script; its first line names its interpreter.
  py      a Python console script installed in <python-lib> (a directory of
          installed distributions). Its distribution and the distributions it
          requires are copied to lib/python. The interpreter is not carried:
          the target provides python3.

--requires carries what the tool's owner declared the tool runs on the target:
<command> alone is needed by every run, <command>: <use> by that use only.

A tool with nothing to check on the target is copied to bin/<name>. Any other
tool's bin/<name> is a launcher that checks, before the tool starts, for the
interpreter and for every command each run needs, says in one line which is
missing, and then runs the tool (a script or executable from libexec/<name>, a
Python tool from lib/python, both found relative to the launcher).

<executive> is one of the bundle's executives; its ELF header names the
architecture every staged executable and compiled Python module must match. A
tool that does not match fails here, so the failure is a build error and not
the user's first run.

REQUIREMENTS.txt in <dest> lists what the target must provide for each tool.
"""

import argparse
import importlib.metadata
import json
import operator
import os
import platform
import re
import shutil
import stat
import struct
import sys

ELF_MACHINES = {
    0x03: "x86",
    0x28: "arm",
    0x3E: "x86_64",
    0xB7: "aarch64",
    0xF3: "riscv",
}


class StageError(Exception):
    """A tool that cannot be staged for this package."""


def elf_identity(path):
    """Return (machine, bits, byte order) for an ELF file, or None for any other file."""
    with open(path, "rb") as handle:
        head = handle.read(20)
    if len(head) < 20 or head[:4] != b"\x7fELF":
        return None
    bits = {1: 32, 2: 64}.get(head[4])
    order = {1: "<", 2: ">"}.get(head[5])
    if bits is None or order is None:
        return None
    (machine,) = struct.unpack(order + "H", head[18:20])
    return machine, bits, order


def describe(identity):
    """Name an ELF identity for a message."""
    machine, bits, order = identity
    name = ELF_MACHINES.get(machine, "machine 0x%x" % machine)
    return "%s (%d-bit, %s-endian)" % (name, bits, "little" if order == "<" else "big")


def require_match(bundle, tool, path, reference):
    """Fail when an ELF file is built for another architecture than the package."""
    identity = elf_identity(path)
    if identity is not None and identity != reference:
        raise StageError(
            "bundle %s: tool %s cannot be staged: %s is built for %s and the package is %s"
            % (bundle, tool, path, describe(identity), describe(reference))
        )
    return identity


def tree_size(paths):
    """Total size in bytes of the given files."""
    return sum(os.path.getsize(p) for p in paths if os.path.isfile(p))


# A package is unpacked and run by users other than the one who built it: its executables are
# readable and runnable by all and writable by the owner alone, whatever the builder's umask.
PACKAGED_EXECUTABLE_MODE = 0o755


def copy_executable(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    # nosemgrep: python.lang.security.audit.insecure-file-permissions.insecure-file-permissions
    os.chmod(dst, PACKAGED_EXECUTABLE_MODE)


def normalize(name):
    """Distribution and extra names compare case-insensitively with -, _ and . equal."""
    return re.sub(r"[-_.]+", "-", name).lower()


# How an environment marker's variables are read. A requirement's marker is
# evaluated for the python3 that runs this script, which stands for the
# interpreter the library was installed for; each class says what the target
# has to share with it for the answer to hold there too.
MARKER_VARIABLES = {
    # The extras this closure activates.
    "extra": "extra",
    # The interpreter's release numbers. The launcher checks that each
    # comparison read here gives the target the answer it gave the build.
    "python_version": "version",
    "python_full_version": "version",
    # The launcher holds the target to the build's implementation.
    "platform_python_implementation": "implementation",
    "implementation_name": "implementation",
    # Linux on both sides.
    "os_name": "linux",
    "sys_platform": "linux",
    "platform_system": "linux",
    # Known for a package of the build machine's own architecture only.
    "platform_machine": "machine",
    # The target's kernel is unknown to the build.
    "platform_release": "kernel",
    "platform_version": "kernel",
    # Not read: a launcher checks the interpreter's version, not its
    # implementation's.
    "implementation_version": "implementation-version",
}

MARKER_TEXT = {
    "platform_python_implementation": platform.python_implementation,
    "implementation_name": lambda: sys.implementation.name,
    "os_name": lambda: os.name,
    "sys_platform": lambda: sys.platform,
    "platform_system": platform.system,
    "platform_machine": platform.machine,
}

# Why a requirement whose marker the build cannot answer is refused, by class.
MARKER_UNKNOWN = {
    "kernel": "depends on the kernel version of the machine it runs on, which the build "
    "does not know",
    "machine": "depends on the machine, and its library was installed on another "
    "architecture than the package's",
    "implementation-version": "depends on the version of the Python implementation, which "
    "a launcher does not check",
}

MARKER_TOKEN = re.compile(
    r"\s*(?:(?P<op>===|==|~=|!=|<=|>=|<|>|\(|\))"
    r"|(?P<text>'[^']*'|\"[^\"]*\")"
    r"|(?P<word>[A-Za-z_][A-Za-z0-9_.]*))"
)
REQUIREMENT_HEAD = re.compile(
    r"\s*([A-Za-z0-9](?:[A-Za-z0-9._-]*[A-Za-z0-9])?)\s*(?:\[([^\]]*)\])?"
)
COMPARISONS = ("===", "==", "~=", "!=", "<=", ">=", "<", ">")
COMPARE = {
    "==": operator.eq,
    "!=": operator.ne,
    "<": operator.lt,
    "<=": operator.le,
    ">": operator.gt,
    ">=": operator.ge,
}
# The comparison that holds exactly when the given one does not, and the one
# that says the same with its two sides exchanged. The exchange is sound for
# release numbers only: against a pre-release, "3.11.0rc1" < v and
# v > "3.11.0rc1" give release 3.11.0 different answers.
OPPOSITE = {"<": ">=", ">=": "<", "<=": ">", ">": "<=", "==": "!=", "!=": "=="}
EXCHANGED = {"<": ">", ">": "<", "<=": ">=", ">=": "<=", "==": "==", "!=": "!="}
RELEASE = re.compile(r"(\d+(?:\.\d+){0,2})(\.\*|(?:a|b|rc)\d+)?")
# A version in any spelling PEP 440 normalizes, as it may follow == or !=: a
# leading v, an epoch, pre-release, post-release, development and local parts
# under their alternative names and separators, or a trailing .*; in either
# case, with spaces around it.
VERSION_LIKE = re.compile(
    r"""\s*v?(?:[0-9]+!)?[0-9]+(?:\.[0-9]+)*
        (?:\.\*
          |(?:[-_.]?(?:alpha|beta|preview|pre|a|b|c|rc)[-_.]?[0-9]*)?
           (?:-[0-9]+|[-_.]?(?:post|rev|r)[-_.]?[0-9]*)?
           (?:[-_.]?dev[-_.]?[0-9]*)?
           (?:\+[a-z0-9]+(?:[-_.][a-z0-9]+)*)?
        )?\s*""",
    re.VERBOSE | re.IGNORECASE,
)


def release_terms(comparison, version, marker=False):
    """What `<comparison><version>` asks of an interpreter's (major, minor, micro).

    Returns [(length, comparison, numbers)]: every term has to hold, and one
    holds when the first <length> of the interpreter's numbers compare so with
    <numbers>. Returns None for a clause outside what is read here: a version
    is up to three numbers, with a trailing .* for == and != only, and ~=
    takes at least two.

    A marker (not a Requires-Python clause) may also order the interpreter
    against a pre-release such as 3.11.0a7, which sorts just before its
    release; in exchange it has no ~=, so that every comparison a marker makes
    has an opposite. An interpreter is read by its release numbers alone, so a
    pre-release interpreter counts as its release.
    """
    match = RELEASE.fullmatch(version)
    if match is None or comparison not in COMPARE and comparison != "~=":
        return None
    parts = tuple(int(n) for n in match.group(1).split("."))
    full = parts + (0,) * (3 - len(parts))
    suffix = match.group(2) or ""
    if suffix == ".*":
        return [(len(parts), comparison, parts)] if comparison in ("==", "!=") else None
    if suffix:
        if not marker or comparison not in ("<", "<=", ">", ">="):
            return None
        return [(3, "<" if comparison in ("<", "<=") else ">=", full)]
    if comparison == "~=":
        if marker or len(parts) < 2:
            return None
        return [(3, ">=", full), (len(parts) - 1, "==", parts[:-1])]
    return [(3, comparison, full)]


def reads_as_version(text):
    """Whether an installer may take a string compared with == or != for a version.

    Some installers try the comparison as a version specifier before they
    compare two strings: they then read <text> as a version when it is spelled
    like one, and as the operand of === (any text at all) when it starts with
    another =. Faced with such a string and a value that is no version, they
    fail where others compare as text, so the comparison has no one answer.
    """
    return text.lstrip().startswith("=") or VERSION_LIKE.fullmatch(text) is not None


def terms_hold(terms, numbers):
    """Whether an interpreter with these (major, minor, micro) meets every term."""
    return all(COMPARE[comparison](numbers[:length], want) for length, comparison, want in terms)


def terms_check(terms, subject="v"):
    """The same test as Python text for a launcher, over the name <subject>."""
    return " and ".join(
        "%s %s %r" % (subject if length == 3 else "%s[:%d]" % (subject, length), comparison, want)
        for length, comparison, want in terms
    )


def any3(values):
    """Three-valued or: None stands for 'not answered'."""
    return True if True in values else (None if None in values else False)


def all3(values):
    """Three-valued and."""
    return False if False in values else (None if None in values else True)


def parse_marker(text):
    """Read an environment marker into a tree of its comparisons.

    A node is ("and" | "or", [nodes]), ("extra", name, whether it has to be
    active), ("version", variable, comparison, version) with the variable on
    the left, ("text", variable, comparison, quoted string, whether the
    variable is on the left), or ("unknown", class) for a variable the build
    cannot answer for the target. A marker outside the grammar, or a comparison
    outside what is read here, raises ValueError.
    """
    tokens = []
    position = 0
    text = text.strip()
    while position < len(text):
        match = MARKER_TOKEN.match(text, position)
        if match is None:
            raise ValueError("unexpected text at %r" % text[position:])
        tokens.append((match.lastgroup, match.group(match.lastgroup)))
        position = match.end()
    tokens.reverse()

    def peek():
        return tokens[-1] if tokens else (None, None)

    def operand():
        kind, value = tokens.pop() if tokens else (None, None)
        if kind == "text":
            return "text", value[1:-1]
        if kind == "word" and value in MARKER_VARIABLES:
            return "variable", value
        raise ValueError("expected a quoted string or a marker variable, not %r" % (value,))

    def comparison():
        if peek() == ("op", "("):
            tokens.pop()
            value = alternatives()
            if peek() != ("op", ")"):
                raise ValueError("unbalanced parentheses")
            tokens.pop()
            return value
        left = operand()
        kind, compare = tokens.pop() if tokens else (None, None)
        if (kind, compare) == ("word", "not") and peek() == ("word", "in"):
            tokens.pop()
            compare = "not in"
        elif not (kind == "op" and compare in COMPARISONS) and (kind, compare) != ("word", "in"):
            raise ValueError("expected a comparison, not %r" % (compare,))
        right = operand()
        if (left[0] == "variable") == (right[0] == "variable"):
            raise ValueError("a comparison is between one marker variable and one quoted string")
        first = left[0] == "variable"
        variable, literal = (left[1], right[1]) if first else (right[1], left[1])
        reads = MARKER_VARIABLES[variable]
        if reads == "extra":
            name = normalize(literal)
            if compare not in ("==", "!=") or not first:
                raise ValueError("an extra is read on the left of == or != against one name")
            if reads_as_version(name):
                raise ValueError("the extra %r reads as a version" % literal)
            return "extra", name, compare == "=="
        if reads in MARKER_UNKNOWN and reads != "machine":
            return "unknown", reads
        if reads == "version":
            if not first:
                plain = re.fullmatch(r"\d+(?:\.\d+){0,2}", literal)
                compare = EXCHANGED.get(compare) if plain else None
            if release_terms(compare, literal, marker=True) is None:
                raise ValueError(
                    "%s is read with <, <=, >, >=, == or != against up to three numbers; on "
                    "its right side also with == or != against a trailing .* and ordered "
                    "against a pre-release" % variable
                )
            return "version", variable, compare, literal
        if compare not in ("==", "!=", "in", "not in"):
            raise ValueError("%s is text, read with ==, !=, in or not in" % variable)
        if compare in ("==", "!=") and reads_as_version(literal):
            raise ValueError("%s is text, and %r reads as a version" % (variable, literal))
        return "text", variable, compare, literal, first

    def conjunction():
        values = [comparison()]
        while peek() == ("word", "and"):
            tokens.pop()
            values.append(comparison())
        return ("and", values) if len(values) > 1 else values[0]

    def alternatives():
        values = [conjunction()]
        while peek() == ("word", "or"):
            tokens.pop()
            values.append(conjunction())
        return ("or", values) if len(values) > 1 else values[0]

    tree = alternatives()
    if tokens:
        raise ValueError("unexpected %r" % (tokens[-1][1],))
    return tree


def marker_value(tree, extra, answer):
    """Evaluate a marker tree for one active extra ('' for none).

    <answer> gives a comparison with the environment its value: True, False,
    or None when it is not answered. Every comparison is asked, whatever the
    others gave.
    """
    if tree[0] in ("and", "or"):
        values = [marker_value(node, extra, answer) for node in tree[1]]
        return all3(values) if tree[0] == "and" else any3(values)
    if tree[0] == "extra":
        return (tree[1] == extra) == tree[2]
    return answer(tree)


def read_requirement(requirement):
    """Read one Requires-Dist entry: (name, extras it requests, marker tree or None)."""
    head, _, marker = requirement.partition(";")
    match = REQUIREMENT_HEAD.match(head)
    if match is None:
        raise ValueError("no distribution name")
    requested = {normalize(e.strip()) for e in (match.group(2) or "").split(",") if e.strip()}
    return normalize(match.group(1)), requested, parse_marker(marker) if marker.strip() else None


def python_closure(bundle, tool, lib, reference):
    """Find the distribution that provides console script <tool> and everything it requires.

    Returns (owner, entry point value, [distributions], [version comparisons],
    whether the implementation was asked). The closure follows each requirement
    with the extras it requests, and decides each marker for the active extras
    and the build's interpreter: a requirement the marker selects has to be
    installed and is carried, and one it does not select is left out even when
    it is installed, since a library holds what other tools need too.

    A marker the extras alone decide holds on every target. For any other, each
    comparison it makes with the interpreter's version is returned in the form
    that is true for the build, as (variable, comparison, version), so the
    launcher can hold the target to the same answers; so is whether it asked
    for the implementation. A marker that turns on something the build cannot
    answer for the target is an error.
    """
    by_name = {}
    owner = None
    entry = None
    for dist in importlib.metadata.distributions(path=[lib]):
        by_name[normalize(dist.metadata["Name"])] = dist
        for point in dist.entry_points:
            if point.group == "console_scripts" and point.name == tool:
                owner, entry = dist, point.value
    if owner is None:
        raise StageError(
            "bundle %s: tool %s cannot be staged: no distribution under %s "
            "declares that console script" % (bundle, tool, lib)
        )
    refused = "bundle %s: tool %s cannot be staged: " % (bundle, tool)
    native = elf_identity(os.path.realpath(sys.executable)) == reference
    numbers = {
        "python_version": tuple(sys.version_info[:2]) + (0,),
        "python_full_version": tuple(sys.version_info[:3]),
    }
    versions = set()
    asked = set()

    def answer(node):
        if node[0] == "version":
            _, variable, compare, version = node
            holds = terms_hold(release_terms(compare, version, marker=True), numbers[variable])
            versions.add((variable, compare if holds else OPPOSITE[compare], version))
            return holds
        reads = node[1] if node[0] == "unknown" else MARKER_VARIABLES[node[1]]
        asked.add(reads)
        if node[0] == "unknown" or (reads == "machine" and not native):
            return None
        _, variable, compare, literal, first = node
        value = MARKER_TEXT[variable]()
        left, right = (value, literal) if first else (literal, value)
        if compare in ("in", "not in"):
            return (left in right) == (compare == "in")
        return (left == right) == (compare == "==")

    start = normalize(owner.metadata["Name"])
    active = {start: set()}
    queue = [start]
    held = set()
    implementation = False
    while queue:
        name = queue.pop()
        for requirement in by_name[name].requires or []:
            try:
                required, requested, marker = read_requirement(requirement)
            except ValueError as error:
                raise StageError(
                    refused + "cannot read %s's requirement %r: %s" % (name, requirement, error)
                ) from None
            extras = sorted(active[name]) or [""]
            selected = marker is None or any3(
                [marker_value(marker, extra, lambda node: None) for extra in extras]
            )
            if selected is None:
                versions.clear()
                asked.clear()
                selected = any3([marker_value(marker, extra, answer) for extra in extras])
                if selected is None:
                    raise StageError(
                        refused
                        + "%s's requirement %r %s"
                        % (
                            name,
                            requirement,
                            MARKER_UNKNOWN[sorted(asked & set(MARKER_UNKNOWN))[0]],
                        )
                    )
                held |= versions
                implementation = implementation or "implementation" in asked
            if not selected:
                continue
            if required not in by_name:
                raise StageError(
                    refused
                    + "%s requires %s, which is not installed under %s" % (name, required, lib)
                )
            if required not in active or not requested <= active[required]:
                active.setdefault(required, set()).update(requested)
                queue.append(required)
    return owner, entry, [by_name[name] for name in sorted(active)], sorted(held), implementation


def python_clauses(bundle, tool, dists):
    """Read Requires-Python of every staged distribution.

    Returns [(clause text, expression over v)], where v is the interpreter's
    (major, minor, micro). Every clause of every distribution has to hold, so
    the launcher checks them all; the >= clauses are one clause, the highest,
    and the rest follow once each in the order met. A clause release_terms
    does not read is refused.
    """
    floor = None
    clauses = {}
    for dist in dists:
        declared = dist.metadata.get("Requires-Python") or ""
        for clause in filter(None, (c.strip() for c in declared.split(","))):
            match = re.fullmatch(r"(~=|===|==|!=|<=|>=|<|>)\s*(\S+)", clause)
            terms = release_terms(match.group(1), match.group(2)) if match else None
            if terms is None:
                raise StageError(
                    "bundle %s: tool %s cannot be staged: %s declares Requires-Python %r, "
                    "and the launcher cannot check the clause %r"
                    % (bundle, tool, dist.metadata["Name"], declared, clause)
                )
            text = match.group(1) + match.group(2)
            if match.group(1) == ">=":
                floor = max(floor or (terms[0][2], text), (terms[0][2], text))
            else:
                clauses.setdefault(text, terms_check(terms))
    lowest = [(floor[1], "v >= %r" % (floor[0],))] if floor else []
    return lowest + list(clauses.items())


def write_launcher(dest, tool, needs, commands, run):
    """Write bin/<tool>: check for each of <commands>, then run the tool.

    <run> is the shell text that starts the tool; $here is the launcher's own
    directory. <needs> is the tool's line of REQUIREMENTS.txt, shown when a
    command is missing.
    """
    if re.search(r"['\"$`\\\n]", needs):
        raise StageError("tool %s: its requirements hold text a launcher cannot quote" % tool)
    launcher = os.path.join(dest, "bin", tool)
    os.makedirs(os.path.dirname(launcher), exist_ok=True)
    with open(launcher, "w", encoding="utf-8") as handle:
        handle.write(
            "#!/bin/sh\n"
            "# %s: runs from this package. Needs %s on this machine.\n"
            'case "$0" in */*) here=${0%%/*} ;; *) here=. ;; esac\n'
            'here=$(CDPATH= cd -- "$here" && pwd)\n'
            "for need in %s; do\n"
            '  if ! command -v "$need" >/dev/null 2>&1; then\n'
            '    echo "%s: no $need was found on this machine; it needs %s" >&2\n'
            "    exit 127\n"
            "  fi\n"
            "done\n"
            'exec %s "$@"\n' % (tool, needs, " ".join(commands), tool, needs, run)
        )
    # nosemgrep: python.lang.security.audit.insecure-file-permissions.insecure-file-permissions
    os.chmod(launcher, PACKAGED_EXECUTABLE_MODE)
    return launcher


def on_path(always, sometimes):
    """The declared commands as REQUIREMENTS.txt text, or '' when there are none."""
    items = list(always) + ["%s (%s)" % pair for pair in sometimes]
    return "; on PATH: " + ", ".join(items) if items else ""


def stage_python(bundle, tool, lib, dest, reference, always=(), sometimes=()):
    """Copy a Python tool's distributions to lib/python and write its launcher."""
    owner, entry, dists, versions, implementation = python_closure(bundle, tool, lib, reference)
    module, _, func = entry.partition(":")
    if not re.fullmatch(r"[A-Za-z_][\w.]*", module) or not re.fullmatch(r"[A-Za-z_][\w.]*", func):
        raise StageError(
            "bundle %s: tool %s has an entry point this launcher cannot call: %r"
            % (bundle, tool, entry)
        )

    out_lib = os.path.join(dest, "lib", "python")
    lib_root = os.path.realpath(lib)
    copied = []
    compiled = False
    for dist in dists:
        if dist.files is None:
            raise StageError(
                "bundle %s: tool %s cannot be staged: %s records no file list"
                % (bundle, tool, dist.metadata["Name"])
            )
        for record in dist.files:
            src = os.path.realpath(os.path.join(lib, str(record)))
            relative = os.path.relpath(src, lib_root)
            # Console scripts are replaced by the launcher, and bytecode is
            # rebuilt by the target's interpreter.
            if relative.startswith("..") or relative.split(os.sep)[0] == "bin":
                continue
            if "__pycache__" in relative.split(os.sep) or not os.path.isfile(src):
                continue
            if require_match(bundle, tool, src, reference) is not None:
                compiled = True
            dst = os.path.join(out_lib, relative)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            shutil.copymode(src, dst)
            copied.append(dst)

    # Which interpreters run this copy: those every Requires-Python clause of
    # every staged distribution allows, of the build's own version when the
    # copy carries compiled modules, of the build's implementation when a
    # requirement was selected by it, and for which each version comparison
    # that selected a requirement gives the answer it gave the build.
    clauses = python_clauses(bundle, tool, [owner] + [d for d in dists if d is not owner])
    checks = [check for _, check in clauses]
    needs = "python3"
    if compiled:
        checks.append("v[:2] == %r" % (tuple(sys.version_info[:2]),))
        needs += " %d.%d exactly (it carries modules compiled for that version)" % (
            sys.version_info[:2]
        )
    if implementation:
        checks.append("sys.implementation.name == %s" % json.dumps(sys.implementation.name))
        needs += " (%s)" % sys.implementation.name
    if clauses:
        needs += " matching " + ", ".join(text for text, _ in clauses)
    if versions:
        checks += [
            terms_check(
                release_terms(compare, version, marker=True),
                "v" if variable == "python_full_version" else "u",
            )
            for variable, compare, version in versions
        ]
        needs += " %s %s (its requirements were selected for that)" % (
            "and" if clauses else "with",
            ", ".join("%s %s %s" % held for held in versions),
        )

    # The launcher hands this one-line program to python3 inside single
    # quotes, so the program itself carries none.
    code = (
        "import sys; lib = sys.argv.pop(1); sys.argv[0] = %s; v = sys.version_info[:3]; "
        "u = v[:2] + (0,); "
        "(%s) or sys.exit(%s + sys.version.split()[0]); "
        "sys.path.insert(0, lib); from %s import %s as run; sys.exit(run())"
    ) % (
        json.dumps(tool),
        " and ".join(checks) or "True",
        json.dumps("%s: needs %s; this machine has python " % (tool, needs)),
        module,
        func,
    )
    if "'" in code:
        raise StageError(
            "bundle %s: tool %s has a name or version text a launcher cannot quote" % (bundle, tool)
        )
    needs += on_path(always, sometimes)
    launcher = write_launcher(
        dest,
        tool,
        needs,
        ["python3"] + list(always),
        "python3 -c '%s' \"$here/../lib/python\"" % code,
    )
    names = ", ".join("%s %s" % (d.metadata["Name"], d.version) for d in dists)
    return copied + [launcher], needs + "; carries " + names


def script_interpreter(bundle, tool, src):
    """The interpreter a script's first line names: a path, or a command found through env."""
    with open(src, "rb") as handle:
        first = handle.readline(256).decode("utf-8", "replace")
    words = first[2:].split() if first.startswith("#!") else []
    if words and os.path.basename(words[0]) == "env":
        words = [w for w in words[1:] if not w.startswith("-") and "=" not in w]
    if not words or not re.fullmatch(r"[A-Za-z0-9/._+-]+", words[0]):
        raise StageError(
            "bundle %s: tool %s cannot be staged: the first line of %s names no interpreter"
            % (bundle, tool, src)
        )
    return words[0]


def stage_file(bundle, tool, kind, src, dest, reference, always, sometimes):
    """Stage a prebuilt executable or a script, behind a launcher when it has needs to check."""
    if not os.path.isfile(src):
        raise StageError(
            "bundle %s: tool %s cannot be staged: %s was not built" % (bundle, tool, src)
        )
    identity = require_match(bundle, tool, src, reference)
    checks = list(always)
    if kind == "bin":
        if identity is None:
            raise StageError(
                "bundle %s: tool %s cannot be staged: %s is not an ELF executable"
                % (bundle, tool, src)
            )
        needs = "the system C library (%s Linux)" % ELF_MACHINES.get(reference[0], "matching")
    else:
        interpreter = script_interpreter(bundle, tool, src)
        if interpreter == "/bin/sh":
            # The launcher itself runs on it.
            needs = "a POSIX shell (/bin/sh)"
        else:
            needs = interpreter
            checks.insert(0, interpreter)
    needs += on_path(always, sometimes)
    if not checks:
        dst = os.path.join(dest, "bin", tool)
        copy_executable(src, dst)
        return [dst], needs
    payload = os.path.join(dest, "libexec", tool)
    copy_executable(src, payload)
    launcher = write_launcher(dest, tool, needs, checks, '"$here/../libexec/%s"' % tool)
    return [payload, launcher], needs


def read_requires(bundle, entries):
    """Sort the --requires arguments into {tool: ([command], [(command, use)])}."""
    declared = {}
    for entry in entries:
        tool, _, need = entry.partition("=")
        command, _, use = (part.strip() for part in need.partition(":"))
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", command) or re.search(
            r"['\"$`\\\n]", use
        ):
            raise StageError(
                "bundle %s: tool %s declares a requirement this cannot read: %r"
                % (bundle, tool, need)
            )
        always, sometimes = declared.setdefault(tool, ([], []))
        if use:
            sometimes.append((command, use))
        else:
            always.append(command)
    return declared


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--dest", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--tool", action="append", default=[])
    parser.add_argument("--requires", action="append", default=[])
    args = parser.parse_args(argv)

    reference = elf_identity(args.reference)
    if reference is None:
        raise StageError("bundle %s: %s is not an ELF executable" % (args.bundle, args.reference))
    declared = read_requires(args.bundle, args.requires)

    rows = []
    for spec in args.tool:
        parts = spec.split(":")
        if len(parts) < 3 or parts[0] not in ("bin", "script", "py"):
            raise StageError("bundle %s: cannot read the tool argument %r" % (args.bundle, spec))
        kind, name, src = parts[0], parts[1], parts[2]
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise StageError("bundle %s: %r is not a tool name" % (args.bundle, name))
        always, sometimes = declared.pop(name, ([], []))
        if kind == "py":
            if len(parts) != 4:
                raise StageError(
                    "bundle %s: tool %s names no Python library directory" % (args.bundle, name)
                )
            files, needs = stage_python(
                args.bundle, name, parts[3], args.dest, reference, always, sometimes
            )
        else:
            files, needs = stage_file(
                args.bundle, name, kind, src, args.dest, reference, always, sometimes
            )
        rows.append((name, kind, tree_size(files), needs))
    if declared:
        raise StageError(
            "bundle %s: requirements declared for tools the bundle does not stage: %s"
            % (args.bundle, ", ".join(sorted(declared)))
        )

    # Everything else under bin/ was installed by CMake (the C++ tools).
    bin_dir = os.path.join(args.dest, "bin")
    staged = {row[0] for row in rows}
    if os.path.isdir(bin_dir):
        for name in sorted(os.listdir(bin_dir)):
            path = os.path.join(bin_dir, name)
            if name in staged or not stat.S_ISREG(os.stat(path).st_mode):
                continue
            require_match(args.bundle, name, path, reference)
            rows.append(
                (
                    name,
                    "cpp",
                    os.path.getsize(path),
                    "nothing beyond the package (libraries in tools/libs)",
                )
            )

    if rows:
        os.makedirs(args.dest, exist_ok=True)
        with open(os.path.join(args.dest, "REQUIREMENTS.txt"), "w", encoding="utf-8") as handle:
            handle.write(
                "Tools in this package, and what each needs on the machine it is copied to.\n"
                "Run them from tools/bin; nothing has to be installed first.\n\n"
            )
            for name, kind, _, needs in sorted(rows):
                handle.write("%s (%s): %s\n" % (name, kind, needs))
        for name, kind, size, _ in sorted(rows):
            print(
                "[bundle] %s: tool %s (%s) staged, %.1f KiB"
                % (args.bundle, name, kind, size / 1024.0)
            )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except StageError as error:
        print("[bundle] ERROR: %s" % error, file=sys.stderr)
        sys.exit(1)
