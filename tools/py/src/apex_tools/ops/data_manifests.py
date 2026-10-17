#!/usr/bin/env python3
"""
data_manifests.py - Inventory of component data manifests, checked against CMake.

Lists every component data manifest under the given roots: a `*_data.toml`
or `apex_data.toml` file whose TOML carries a top-level `component` key.
Application descriptors (an `application` key) are not generator input and
are skipped; a file carrying neither key, or both, is an error. Membership
is decided by a TOML parser, so the spelling and spacing of the key never
change it.

Unless disabled, the inventory is compared with the manifests CMake
registers through `apex_add_data_manifest(...)` in the CMakeLists.txt files
under the same roots. A call is read the way the helper reads it: no
arguments register the default `apex_data.toml` beside the CMakeLists;
`MANIFEST <path>` registers that path, quoted or unquoted, with
`${CMAKE_CURRENT_SOURCE_DIR}` resolved; line comments and bracket comments
(`#[[ ... ]]`) hide a call. Any other argument shape, or a variable this
reader cannot resolve, is an error rather than a guess. The two sets must be
equal and every registered file must exist, so a registration that points
nowhere, a manifest nobody registered, and a descriptor registered as a
manifest all fail. An empty inventory fails.

Usage:
    data_manifests.py --roots demos src apps
    data_manifests.py --roots demos src apps --no-registration-check
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

if sys.version_info >= (3, 11):
    import tomllib
else:
    try:
        import tomllib
    except ModuleNotFoundError:
        import tomli as tomllib  # type: ignore[no-redef]

# ================================ Configuration ================================

MANIFEST_SUFFIX = "_data.toml"
MANIFEST_DEFAULT_NAME = "apex_data.toml"
SKIP_DIR_NAMES = {"build"}
REGISTRATION_RE = re.compile(r"apex_add_data_manifest\s*\(([^)]*)\)")
BRACKET_COMMENT_RE = re.compile(r"#\[(=*)\[.*?\]\1\]", re.S)
# One CMake argument: a quoted argument (escapes carried as written) or an
# unquoted argument (no whitespace, parentheses, quotes or comment marks).
ARGUMENT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"|([^\s()"#;]+)')
SOURCE_DIR_VAR = "${CMAKE_CURRENT_SOURCE_DIR}"


class ManifestInventoryError(Exception):
    """A manifest, descriptor or registration the inventory cannot accept."""


# ================================= Core Logic ==================================


def _is_skipped(relative: Path) -> bool:
    """Return True for paths under a build or hidden directory."""
    return any(part in SKIP_DIR_NAMES or part.startswith(".") for part in relative.parts)


def _normalize(path: Path) -> Path:
    """Collapse `..` and `.` segments without touching the filesystem."""
    return Path(os.path.normpath(path))


def find_candidates(roots: list[Path]) -> list[Path]:
    """Return every `*_data.toml` or `apex_data.toml` under the roots, sorted."""
    found: set[Path] = set()
    for root in roots:
        for path in root.rglob("*.toml"):
            is_manifest_name = path.name == MANIFEST_DEFAULT_NAME or path.name.endswith(
                MANIFEST_SUFFIX
            )
            if is_manifest_name and not _is_skipped(path.relative_to(root)):
                found.add(path)
    return sorted(found)


def classify(path: Path) -> str:
    """Return "component" or "application" for one candidate file.

    Raises:
        ManifestInventoryError: the file is not valid TOML, or it carries
            neither of the two top-level keys, or both.
    """
    try:
        with path.open("rb") as f:
            doc = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ManifestInventoryError(f"{path}: cannot parse: {exc}") from exc
    has_component = "component" in doc
    has_application = "application" in doc
    if has_component and not has_application:
        return "component"
    if has_application and not has_component:
        return "application"
    present = [k for k in ("component", "application") if k in doc] or ["neither"]
    raise ManifestInventoryError(
        f"{path}: expected exactly one of the top-level keys 'component' (a data "
        f"manifest) or 'application' (an application descriptor); found {present}"
    )


def component_manifests(roots: list[Path]) -> list[Path]:
    """Return the component data manifests under the roots, sorted."""
    return [p for p in find_candidates(roots) if classify(p) == "component"]


def _without_comments(text: str) -> str:
    """Drop CMake bracket and line comments so a commented-out call does not count.

    A `#` inside a quoted argument is part of the argument, not a comment.
    """
    text = BRACKET_COMMENT_RE.sub(" ", text)
    kept = []
    for line in text.splitlines():
        quoted = False
        for pos, ch in enumerate(line):
            if ch == '"':
                quoted = not quoted
            elif ch == "#" and not quoted:
                line = line[:pos]
                break
        kept.append(line)
    return "\n".join(kept)


def _manifest_argument(cmake: Path, raw_args: str) -> Path:
    """Resolve one call's argument list to the manifest it registers.

    Raises:
        ManifestInventoryError: an argument shape the helper does not take,
            a MANIFEST keyword without a value, or a variable other than
            CMAKE_CURRENT_SOURCE_DIR in the path.
    """
    tokens = [quoted or unquoted for quoted, unquoted in ARGUMENT_RE.findall(raw_args)]
    tokens = [tok for tok in tokens if tok]
    if not tokens:
        return cmake.parent / MANIFEST_DEFAULT_NAME
    if tokens[0] != "MANIFEST" or len(tokens) != 2:
        raise ManifestInventoryError(
            f"{cmake}: apex_add_data_manifest({raw_args.strip()}) is not a form this "
            f"inventory reads; use no arguments or MANIFEST <path>"
        )
    raw = tokens[1]
    if "${" in raw.replace(SOURCE_DIR_VAR, ""):
        raise ManifestInventoryError(
            f"{cmake}: cannot resolve MANIFEST path '{raw}'; only {SOURCE_DIR_VAR} is understood"
        )
    return Path(raw.replace(SOURCE_DIR_VAR, str(cmake.parent)))


def registered_manifests(roots: list[Path]) -> set[Path]:
    """Return the manifests `apex_add_data_manifest` registers under the roots.

    Raises:
        ManifestInventoryError: a call whose arguments this reader does not
            take, or a MANIFEST path using a variable other than
            CMAKE_CURRENT_SOURCE_DIR.
    """
    registered: set[Path] = set()
    for root in roots:
        for cmake in sorted(root.rglob("CMakeLists.txt")):
            if _is_skipped(cmake.relative_to(root)):
                continue
            text = _without_comments(cmake.read_text(encoding="utf-8"))
            for call in REGISTRATION_RE.finditer(text):
                registered.add(_normalize(_manifest_argument(cmake, call.group(1))))
    return registered


def inventory(roots: list[Path], check_registration: bool = True) -> list[Path]:
    """Return the component manifests, optionally checked against registration.

    Args:
        roots: Directories to search for manifests and CMakeLists.txt files.
        check_registration: Require the discovered set to equal the registered
            set, with every registered file present.

    Returns:
        The component data manifests, sorted.

    Raises:
        ManifestInventoryError: the inventory is empty, a registered manifest
            does not exist, or the discovered and registered sets differ.
    """
    manifests = component_manifests(roots)
    if not manifests:
        raise ManifestInventoryError(
            f"no component data manifests found under {[str(r) for r in roots]}"
        )
    if check_registration:
        discovered = {_normalize(p) for p in manifests}
        registered = registered_manifests(roots)
        problems = []
        missing = sorted(str(p) for p in registered if not p.is_file())
        if missing:
            problems.append("registered but not present: " + ", ".join(missing))
        unregistered = sorted(str(p) for p in discovered - registered)
        if unregistered:
            problems.append(
                "present but not registered by apex_add_data_manifest: " + ", ".join(unregistered)
            )
        foreign = sorted(str(p) for p in registered - discovered if p.is_file())
        if foreign:
            problems.append("registered but not a component data manifest: " + ", ".join(foreign))
        if problems:
            raise ManifestInventoryError("; ".join(problems))
    return manifests


# ================================== Main CLI ===================================


def main(argv: list[str] | None = None) -> int:
    """Print one manifest path per line; exit 1 with a message on any problem."""
    parser = argparse.ArgumentParser(
        description="List component data manifests, checked against CMake registration."
    )
    parser.add_argument(
        "--roots",
        nargs="+",
        type=Path,
        required=True,
        help="directories to search (manifests and CMakeLists.txt)",
    )
    parser.add_argument(
        "--no-registration-check",
        action="store_true",
        help="list the manifests without comparing them to apex_add_data_manifest calls",
    )
    args = parser.parse_args(argv)
    try:
        manifests = inventory(args.roots, check_registration=not args.no_registration_check)
    except ManifestInventoryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    for manifest in manifests:
        print(manifest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
