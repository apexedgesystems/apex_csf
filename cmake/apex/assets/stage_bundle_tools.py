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
import os
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


def copy_executable(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    os.chmod(dst, 0o755)


def normalize(name):
    """Distribution and extra names compare case-insensitively with -, _ and . equal."""
    return re.sub(r"[-_.]+", "-", name).lower()


# How a requirement whose environment marker names a variable is decided for
# the target. The installer evaluated every marker on the build machine, and
# the distributions it left in the library are that decision; each class says
# what the target has to share with the build machine for it to hold.
MARKER_VARIABLES = {
    # The extras this closure activates; evaluated here.
    "extra": "extra",
    # The interpreter: the package then runs on the build's version only.
    "python_version": "interpreter",
    "python_full_version": "interpreter",
    "implementation_version": "interpreter",
    "platform_python_implementation": "implementation",
    "implementation_name": "implementation",
    # Linux on both sides.
    "os_name": "same",
    "sys_platform": "same",
    "platform_system": "same",
    # Holds for a package of the build machine's own architecture only.
    "platform_machine": "machine",
    # The target's kernel is unknown to the build.
    "platform_release": "kernel",
    "platform_version": "kernel",
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


def any3(values):
    """Three-valued or: None stands for 'the installer's environment decided'."""
    return True if True in values else (None if None in values else False)


def all3(values):
    """Three-valued and."""
    return False if False in values else (None if None in values else True)


def marker_value(text, extra, classes):
    """Evaluate an environment marker for one active extra ('' for none).

    Returns True or False when the extra alone decides the marker, and None
    when it also depends on the environment; the classes of the variables it
    names (MARKER_VARIABLES) are added to <classes>. A marker outside the
    grammar raises ValueError.
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
        kind, operator = tokens.pop() if tokens else (None, None)
        if (kind, operator) == ("word", "not") and peek() == ("word", "in"):
            tokens.pop()
            operator = "not in"
        elif not (kind == "op" and operator in COMPARISONS) and (kind, operator) != ("word", "in"):
            raise ValueError("expected a comparison, not %r" % (operator,))
        right = operand()
        names = [value for kind, value in (left, right) if kind == "variable"]
        if "extra" in names:
            literal = [value for kind, value in (left, right) if kind == "text"]
            if operator not in ("==", "!=") or len(literal) != 1:
                raise ValueError("an extra is compared with == or != against one name")
            return (normalize(literal[0]) == extra) == (operator == "==")
        classes.update(MARKER_VARIABLES[name] for name in names)
        return None

    def conjunction():
        values = [comparison()]
        while peek() == ("word", "and"):
            tokens.pop()
            values.append(comparison())
        return all3(values)

    def alternatives():
        values = [conjunction()]
        while peek() == ("word", "or"):
            tokens.pop()
            values.append(conjunction())
        return any3(values)

    value = alternatives()
    if tokens:
        raise ValueError("unexpected %r" % (tokens[-1][1],))
    return value


def read_requirement(requirement, extras):
    """Read one Requires-Dist entry of a distribution whose active extras are <extras>.

    Returns (name, extras it requests, verdict, marker classes). The verdict is
    True when the requirement holds on every target, False when no active extra
    selects it, and None when the installer's environment decided it.
    """
    head, _, marker = requirement.partition(";")
    match = REQUIREMENT_HEAD.match(head)
    if match is None:
        raise ValueError("no distribution name")
    name = normalize(match.group(1))
    requested = {normalize(e) for e in (match.group(2) or "").split(",") if e.strip()}
    if not marker.strip():
        return name, requested, True, set()
    classes = set()
    verdict = any3([marker_value(marker, extra, classes) for extra in sorted(extras) or [""]])
    return name, requested, verdict, classes


def python_closure(bundle, tool, lib, reference):
    """Find the distribution that provides console script <tool> and everything it requires.

    Returns (owner, entry point value, [distributions], marker classes). The
    closure follows each requirement with the extras it requests. A requirement
    no active extra selects is left out even when it is installed; one that
    holds on every target and is not installed is an error; one whose marker
    names the environment is taken as the installer left it, and the classes
    of what it names are returned so the launcher can hold the target to them.
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
    build = elf_identity(os.path.realpath(sys.executable))
    start = normalize(owner.metadata["Name"])
    active = {start: set()}
    queue = [start]
    held = set()
    while queue:
        name = queue.pop()
        for requirement in by_name[name].requires or []:
            try:
                required, requested, verdict, classes = read_requirement(requirement, active[name])
            except ValueError as error:
                raise StageError(
                    refused + "cannot read %s's requirement %r: %s" % (name, requirement, error)
                ) from None
            if verdict is False:
                continue
            if "kernel" in classes:
                raise StageError(
                    refused + "%s's requirement %r depends on the kernel version of the machine "
                    "it runs on, which the build does not know" % (name, requirement)
                )
            if "machine" in classes and build != reference:
                raise StageError(
                    refused + "%s's requirement %r depends on the machine, and %s was installed "
                    "on another architecture than the package's" % (name, requirement, lib)
                )
            held |= classes & {"interpreter", "implementation"}
            if required not in by_name:
                if verdict is True:
                    raise StageError(
                        refused
                        + "%s requires %s, which is not installed under %s" % (name, required, lib)
                    )
                continue
            if required not in active or not requested <= active[required]:
                active.setdefault(required, set()).update(requested)
                queue.append(required)
    return owner, entry, [by_name[name] for name in sorted(active)], held


SPECIFIER = re.compile(r"(~=|===|==|!=|<=|>=|<|>)\s*(\d+(?:\.\d+)*)(\.\*)?")


def python_clauses(bundle, tool, dists):
    """Read Requires-Python of every staged distribution.

    Returns [(clause text, expression over v)], where v is the interpreter's
    (major, minor, micro). Every clause of every distribution has to hold, so
    the launcher checks them all; the >= clauses are one clause, the highest,
    and the rest follow once each in the order met. A clause this cannot turn
    into a check is refused: versions are up to three numbers, with a trailing
    .* for == and != only.
    """
    floor = None
    clauses = {}
    for dist in dists:
        declared = dist.metadata.get("Requires-Python") or ""
        for clause in filter(None, (c.strip() for c in declared.split(","))):
            match = SPECIFIER.fullmatch(clause)
            operator = match.group(1) if match else None
            parts = tuple(int(n) for n in match.group(2).split(".")) if match else ()
            wildcard = bool(match and match.group(3))
            if (
                match is None
                or operator == "==="
                or len(parts) > 3
                or (wildcard and operator not in ("==", "!="))
                or (operator == "~=" and len(parts) < 2)
            ):
                raise StageError(
                    "bundle %s: tool %s cannot be staged: %s declares Requires-Python %r, "
                    "and the launcher cannot check the clause %r"
                    % (bundle, tool, dist.metadata["Name"], declared, clause)
                )
            full = parts + (0,) * (3 - len(parts))
            text = operator + match.group(2) + (".*" if wildcard else "")
            if operator == ">=":
                floor = max(floor or (full, text), (full, text))
            elif wildcard:
                clauses.setdefault(text, "v[:%d] %s %r" % (len(parts), operator, parts))
            elif operator == "~=":
                check = "v >= %r and v[:%d] == %r" % (full, len(parts) - 1, parts[:-1])
                clauses.setdefault(text, check)
            else:
                clauses.setdefault(text, "v %s %r" % (operator, full))
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
    os.chmod(launcher, 0o755)
    return launcher


def on_path(always, sometimes):
    """The declared commands as REQUIREMENTS.txt text, or '' when there are none."""
    items = list(always) + ["%s (%s)" % pair for pair in sometimes]
    return "; on PATH: " + ", ".join(items) if items else ""


def stage_python(bundle, tool, lib, dest, reference, always=(), sometimes=()):
    """Copy a Python tool's distributions to lib/python and write its launcher."""
    owner, entry, dists, held = python_closure(bundle, tool, lib, reference)
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

    # Which interpreters run this copy: every Requires-Python clause of every
    # staged distribution, the build's own version when compiled modules or a
    # version-dependent requirement tie the copy to it, and the build's
    # implementation when a requirement was selected for it.
    clauses = python_clauses(bundle, tool, [owner] + [d for d in dists if d is not owner])
    checks = [check for _, check in clauses]
    here = "%d.%d" % sys.version_info[:2]
    needs = "python3"
    if compiled or "interpreter" in held:
        checks.append("v[:2] == %r" % (tuple(sys.version_info[:2]),))
        needs += " %s exactly (%s)" % (
            here,
            (
                "it carries modules compiled for that version"
                if compiled
                else "its requirements were selected for that version"
            ),
        )
    if "implementation" in held:
        checks.append("sys.implementation.name == %s" % json.dumps(sys.implementation.name))
        needs += " (%s)" % sys.implementation.name
    if clauses:
        needs += " matching " + ", ".join(text for text, _ in clauses)

    # The launcher hands this one-line program to python3 inside single
    # quotes, so the program itself carries none.
    code = (
        "import sys; lib = sys.argv.pop(1); sys.argv[0] = %s; v = sys.version_info[:3]; "
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
