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

Kinds:
  bin     a prebuilt executable (a Rust tool). Copied to bin/<name>.
  script  a shell script. Copied to bin/<name>.
  py      a Python console script installed in <python-lib> (a directory of
          installed distributions). Its distribution and the distributions it
          requires are copied to lib/python, and bin/<name> becomes a launcher
          that finds lib/python relative to itself. The interpreter is not
          carried: the target provides python3.

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
    """Distribution names compare case-insensitively with -, _ and . equal."""
    return re.sub(r"[-_.]+", "-", name).lower()


def requirement_name(requirement):
    """Split a requirement string into (name, has a marker, is for an extra only)."""
    spec, _, marker = requirement.partition(";")
    name = re.match(r"\s*([A-Za-z0-9][A-Za-z0-9._-]*)", spec)
    if name is None:
        raise StageError("cannot read the requirement %r" % requirement)
    return normalize(name.group(1)), bool(marker.strip()), "extra" in marker


def python_closure(bundle, tool, lib):
    """Find the distribution that provides console script <tool> and everything it requires.

    Returns (entry point value, [distributions]). A requirement with an
    environment marker that is absent from <lib> was left out by the installer
    for this environment; one without a marker that is absent is an error.
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
    closure = {}
    queue = [normalize(owner.metadata["Name"])]
    while queue:
        name = queue.pop()
        if name in closure:
            continue
        closure[name] = by_name[name]
        for requirement in by_name[name].requires or []:
            required, has_marker, extra_only = requirement_name(requirement)
            if extra_only:
                continue
            if required in by_name:
                queue.append(required)
            elif not has_marker:
                raise StageError(
                    "bundle %s: tool %s cannot be staged: %s requires %s, "
                    "which is not installed under %s" % (bundle, tool, name, required, lib)
                )
    return entry, [closure[name] for name in sorted(closure)]


def stage_python(bundle, tool, lib, dest, reference):
    """Copy a Python tool's distributions to lib/python and write its launcher."""
    entry, dists = python_closure(bundle, tool, lib)
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

    # Compiled modules are built for one interpreter version; pure Python runs
    # on any python3 the tool's own metadata allows.
    here = "%d.%d" % sys.version_info[:2]
    owner_dist = next(d for d in dists if any(p.name == tool for p in d.entry_points))
    declared = owner_dist.metadata.get("Requires-Python")
    if compiled:
        check = "sys.version_info[:2] == (%d, %d)" % sys.version_info[:2]
        needs = "python3 %s exactly (it carries modules compiled for that version)" % here
    else:
        floor = re.search(r">=\s*(\d+)\.(\d+)", declared or "")
        major, minor = (int(floor.group(1)), int(floor.group(2))) if floor else (3, 0)
        check = "sys.version_info[:2] >= (%d, %d)" % (major, minor)
        needs = "python3 %d.%d or newer" % (major, minor)

    # The launcher is a shell script that hands this one-line program to
    # python3 inside single quotes, so the program itself carries none.
    code = (
        "import sys; lib = sys.argv.pop(1); sys.argv[0] = %s; "
        "%s or sys.exit(%s + sys.version.split()[0]); "
        "sys.path.insert(0, lib); from %s import %s as run; sys.exit(run())"
    ) % (
        json.dumps(tool),
        check,
        json.dumps("%s: needs %s; this machine has python " % (tool, needs)),
        module,
        func,
    )
    if "'" in code or "'" in needs:
        raise StageError(
            "bundle %s: tool %s has a name or version text a launcher cannot quote" % (bundle, tool)
        )
    launcher = os.path.join(dest, "bin", tool)
    os.makedirs(os.path.dirname(launcher), exist_ok=True)
    with open(launcher, "w", encoding="utf-8") as handle:
        handle.write(
            "#!/bin/sh\n"
            "# %s: runs from this package. Needs %s on this machine.\n"
            'case "$0" in */*) here=${0%%/*} ;; *) here=. ;; esac\n'
            'here=$(CDPATH= cd -- "$here" && pwd)\n'
            "if ! command -v python3 >/dev/null 2>&1; then\n"
            "  echo '%s: needs %s; no python3 was found on this machine' >&2\n"
            "  exit 127\n"
            "fi\n"
            'exec python3 -c \'%s\' "$here/../lib/python" "$@"\n' % (tool, needs, tool, needs, code)
        )
    os.chmod(launcher, 0o755)
    names = ", ".join("%s %s" % (d.metadata["Name"], d.version) for d in dists)
    return copied + [launcher], needs + "; carries " + names


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--dest", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--tool", action="append", default=[])
    args = parser.parse_args(argv)

    reference = elf_identity(args.reference)
    if reference is None:
        raise StageError("bundle %s: %s is not an ELF executable" % (args.bundle, args.reference))

    rows = []
    for spec in args.tool:
        parts = spec.split(":")
        if len(parts) < 3 or parts[0] not in ("bin", "script", "py"):
            raise StageError("bundle %s: cannot read the tool argument %r" % (args.bundle, spec))
        kind, name, src = parts[0], parts[1], parts[2]
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise StageError("bundle %s: %r is not a tool name" % (args.bundle, name))
        if kind == "py":
            if len(parts) != 4:
                raise StageError(
                    "bundle %s: tool %s names no Python library directory" % (args.bundle, name)
                )
            files, needs = stage_python(args.bundle, name, parts[3], args.dest, reference)
        else:
            if not os.path.isfile(src):
                raise StageError(
                    "bundle %s: tool %s cannot be staged: %s was not built"
                    % (args.bundle, name, src)
                )
            identity = require_match(args.bundle, name, src, reference)
            if kind == "bin" and identity is None:
                raise StageError(
                    "bundle %s: tool %s cannot be staged: %s is not an ELF executable"
                    % (args.bundle, name, src)
                )
            dst = os.path.join(args.dest, "bin", name)
            copy_executable(src, dst)
            files = [dst]
            needs = (
                "the system C library of a %s Linux" % ELF_MACHINES.get(reference[0], "matching")
                if kind == "bin"
                else "a POSIX shell and the commands the script calls"
            )
        rows.append((name, kind, tree_size(files), needs))

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
