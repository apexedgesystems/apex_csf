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
under the same roots. The files are read with a small CMake lexer, so a
comment opener inside a line comment or a quoted argument is text, and a
parenthesis or bracket inside a quoted argument stays in the argument. A
call is read the way the helper reads it: no arguments register the default
`apex_data.toml` beside the CMakeLists; `MANIFEST <path>` registers that
path, quoted or unquoted, with `${CMAKE_CURRENT_SOURCE_DIR}` resolved; line
comments and bracket comments (`#[[ ... ]]`) hide a call. Any other argument
shape, or a variable this reader cannot resolve, is an error rather than a
guess. The two sets must be
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
REGISTRATION_COMMAND = "apex_add_data_manifest"
BRACKET_OPEN_RE = re.compile(r"\[(=*)\[")
SOURCE_DIR_VAR = "${CMAKE_CURRENT_SOURCE_DIR}"
_SEPARATORS = ' \t\r\n()"#'


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


Token = tuple[str, str]


def _lex(cmake: Path, text: str) -> list[Token]:
    """Split a CMakeLists file into tokens with comments removed.

    Token kinds: "(" and ")", "quoted" (escapes resolved), "bracket"
    (a `[[ ... ]]` argument), and "unquoted" (identifiers and bare
    arguments). A `#` outside a quoted or bracket argument starts a line
    comment, or a bracket comment when `[=*[` follows it immediately; the
    comment runs to its own end, so an opener inside a line comment or a
    quoted value is text.

    Raises:
        ManifestInventoryError: an unterminated quoted argument, bracket
            argument or bracket comment.
    """
    tokens: list[Token] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch in " \t\r\n":
            i += 1
        elif ch == "#":
            opener = BRACKET_OPEN_RE.match(text, i + 1)
            if opener is not None:
                i = _bracket_end(cmake, text, opener, "bracket comment")
            else:
                newline = text.find("\n", i)
                i = n if newline < 0 else newline + 1
        elif ch == '"':
            value = []
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    value.append(text[j + 1])
                    j += 2
                else:
                    value.append(text[j])
                    j += 1
            if j >= n:
                raise ManifestInventoryError(f"{cmake}: unterminated quoted argument")
            tokens.append(("quoted", "".join(value)))
            i = j + 1
        elif ch in "()":
            tokens.append((ch, ch))
            i += 1
        else:
            opener = BRACKET_OPEN_RE.match(text, i)
            if opener is not None:
                end = _bracket_end(cmake, text, opener, "bracket argument")
                close_len = len(opener.group(1)) + 2
                tokens.append(("bracket", text[opener.end() : end - close_len]))
                i = end
            else:
                j = i
                while j < n and text[j] not in _SEPARATORS:
                    j += 1
                tokens.append(("unquoted", text[i:j]))
                i = j
    return tokens


def _bracket_end(cmake: Path, text: str, opener: re.Match[str], what: str) -> int:
    """Return the index just past the `]=*]` that closes the opener."""
    close = "]" + opener.group(1) + "]"
    end = text.find(close, opener.end())
    if end < 0:
        raise ManifestInventoryError(f"{cmake}: unterminated {what}")
    return end + len(close)


def _registration_calls(cmake: Path, tokens: list[Token]) -> list[list[Token]]:
    """Return the argument tokens of each registration call, in file order."""
    calls: list[list[Token]] = []
    i = 0
    while i < len(tokens):
        kind, value = tokens[i]
        if kind == "unquoted" and value.lower() == REGISTRATION_COMMAND and i + 1 < len(tokens):
            if tokens[i + 1][0] != "(":
                i += 1
                continue
            depth = 0
            args: list[Token] = []
            j = i + 1
            while j < len(tokens):
                if tokens[j][0] == "(":
                    depth += 1
                    if depth > 1:
                        args.append(tokens[j])
                elif tokens[j][0] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                    args.append(tokens[j])
                else:
                    args.append(tokens[j])
                j += 1
            if depth != 0:
                raise ManifestInventoryError(f"{cmake}: unterminated {REGISTRATION_COMMAND} call")
            calls.append(args)
            i = j + 1
        else:
            i += 1
    return calls


def _manifest_argument(cmake: Path, args: list[Token]) -> Path:
    """Resolve one call's argument tokens to the manifest it registers.

    Raises:
        ManifestInventoryError: an argument shape the helper does not take,
            a MANIFEST keyword without a value, or a variable other than
            CMAKE_CURRENT_SOURCE_DIR in the path.
    """
    if not args:
        return cmake.parent / MANIFEST_DEFAULT_NAME
    shown = " ".join(value for _, value in args)
    if len(args) != 2 or args[0] != ("unquoted", "MANIFEST") or args[1][0] == "(":
        raise ManifestInventoryError(
            f"{cmake}: {REGISTRATION_COMMAND}({shown}) is not a form this inventory reads; "
            "use no arguments or MANIFEST <path>"
        )
    raw = args[1][1]
    if "${" in raw.replace(SOURCE_DIR_VAR, ""):
        raise ManifestInventoryError(
            f"{cmake}: cannot resolve MANIFEST path '{raw}'; only {SOURCE_DIR_VAR} is understood"
        )
    return Path(raw.replace(SOURCE_DIR_VAR, str(cmake.parent)))


def registered_manifests(roots: list[Path]) -> set[Path]:
    """Return the manifests `apex_add_data_manifest` registers under the roots.

    Raises:
        ManifestInventoryError: a file the lexer cannot finish (an
            unterminated quote, bracket or call), a call whose arguments
            this reader does not take, or a MANIFEST path using a variable
            other than CMAKE_CURRENT_SOURCE_DIR.
    """
    registered: set[Path] = set()
    for root in roots:
        for cmake in sorted(root.rglob("CMakeLists.txt")):
            if _is_skipped(cmake.relative_to(root)):
                continue
            tokens = _lex(cmake, cmake.read_text(encoding="utf-8"))
            for args in _registration_calls(cmake, tokens):
                registered.add(_normalize(_manifest_argument(cmake, args)))
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
