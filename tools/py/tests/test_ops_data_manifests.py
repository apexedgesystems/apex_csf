"""Tests for the component data manifest inventory.

Membership comes from parsed TOML and is checked against the
apex_add_data_manifest registrations, so a spelling change cannot narrow
the gate and a registration that points nowhere cannot pass unseen.
"""

from pathlib import Path

import pytest
from apex_tools.ops.data_manifests import ManifestInventoryError, inventory, main


def _write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def _component_tree(root: Path, component_line: str) -> Path:
    """One registered component manifest spelled as the caller wants."""
    manifest = _write(
        root / "comp" / "apex_data.toml",
        f'{component_line}\n[structs]\nA = {{ category = "OUTPUT" }}\n',
    )
    _write(root / "comp" / "CMakeLists.txt", "apex_add_data_manifest()\n")
    return manifest


def test_compact_spelling_is_discovered(tmp_path):
    """Verify `component="X"` with no spaces is in the inventory like any other spelling."""
    manifest = _component_tree(tmp_path, 'component="SystemMonitor"')
    assert inventory([tmp_path]) == [manifest]


def test_application_descriptor_is_not_a_component_manifest(tmp_path):
    """Verify an `application` descriptor is skipped while the manifest beside it is kept."""
    manifest = _component_tree(tmp_path, 'component = "Wave"')
    _write(tmp_path / "app_data.toml", 'application = "Demo"\n[[components]]\nname = "Wave"\n')
    assert inventory([tmp_path]) == [manifest]


def test_file_with_neither_key_fails(tmp_path):
    """Verify a manifest-named file without `component` or `application` is an error."""
    _component_tree(tmp_path, 'component = "Wave"')
    _write(tmp_path / "odd" / "odd_data.toml", '[structs]\nA = { category = "OUTPUT" }\n')
    with pytest.raises(ManifestInventoryError, match="exactly one of the top-level keys"):
        inventory([tmp_path])


def test_registered_manifest_that_does_not_exist_fails(tmp_path):
    """Verify a registration pointing at a missing file fails instead of being skipped."""
    _component_tree(tmp_path, 'component = "Wave"')
    _write(
        tmp_path / "gone" / "CMakeLists.txt",
        'apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/gone_data.toml")\n',
    )
    with pytest.raises(ManifestInventoryError, match="registered but not present"):
        inventory([tmp_path])


def test_unregistered_manifest_fails(tmp_path):
    """Verify a manifest no CMakeLists registers fails the registration check."""
    _component_tree(tmp_path, 'component = "Wave"')
    _write(tmp_path / "loose" / "loose_data.toml", 'component = "Loose"\n')
    with pytest.raises(ManifestInventoryError, match="not registered by apex_add_data_manifest"):
        inventory([tmp_path])


def test_registration_check_can_be_disabled(tmp_path):
    """Verify the unregistered manifest is listed when only discovery is asked for."""
    kept = _component_tree(tmp_path, 'component = "Wave"')
    loose = _write(tmp_path / "loose" / "loose_data.toml", 'component = "Loose"\n')
    assert inventory([tmp_path], check_registration=False) == sorted([kept, loose])


def test_registration_resolves_parent_directory_paths(tmp_path):
    """Verify a MANIFEST path with `..` resolves to the file it names."""
    manifest = _write(tmp_path / "driver" / "driver_data.toml", 'component = "Driver"\n')
    _write(
        tmp_path / "model" / "CMakeLists.txt",
        'apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}'
        '/../driver/driver_data.toml")\n',
    )
    assert inventory([tmp_path]) == [manifest]


def test_commented_registration_does_not_count(tmp_path):
    """Verify a registration inside a CMake comment neither registers nor requires a file."""
    manifest = _component_tree(tmp_path, 'component = "Wave"')
    _write(
        tmp_path / "docs" / "CMakeLists.txt", "# apex_add_data_manifest() registers a manifest\n"
    )
    assert inventory([tmp_path]) == [manifest]


def test_empty_inventory_fails(tmp_path):
    """Verify roots without any component manifest are an error, not an empty success."""
    (tmp_path / "empty").mkdir()
    with pytest.raises(ManifestInventoryError, match="no component data manifests"):
        inventory([tmp_path / "empty"])


def test_cli_prints_one_path_per_line_and_reports_errors(tmp_path, capsys):
    """Verify the command prints the inventory on success and a message with exit 1 on failure."""
    manifest = _component_tree(tmp_path, 'component = "Wave"')
    assert main(["--roots", str(tmp_path)]) == 0
    assert capsys.readouterr().out.strip() == str(manifest)
    _write(tmp_path / "loose" / "loose_data.toml", 'component = "Loose"\n')
    assert main(["--roots", str(tmp_path)]) == 1
    assert "not registered" in capsys.readouterr().err


def test_unquoted_manifest_argument_registers_the_file(tmp_path):
    """Verify `MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/x` without quotes registers x, as CMake does."""
    manifest = _write(tmp_path / "wave" / "wave_data.toml", 'component = "Wave"\n')
    _write(
        tmp_path / "wave" / "CMakeLists.txt",
        "apex_add_data_manifest(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/wave_data.toml)\n",
    )
    assert inventory([tmp_path]) == [manifest]


def test_unquoted_registration_of_a_missing_file_fails(tmp_path):
    """Verify an unquoted path that points nowhere is reported, not read as the default."""
    _component_tree(tmp_path, 'component = "Wave"')
    _write(
        tmp_path / "comp" / "CMakeLists.txt",
        "apex_add_data_manifest(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/missing_data.toml)\n",
    )
    with pytest.raises(ManifestInventoryError, match="registered but not present"):
        inventory([tmp_path])


def test_bracket_comment_hides_a_registration(tmp_path):
    """Verify a call inside `#[[ ... ]]` registers nothing, so its manifest is unregistered."""
    _component_tree(tmp_path, 'component = "Wave"')
    _write(
        tmp_path / "comp" / "CMakeLists.txt",
        "#[[\napex_add_data_manifest()\n]]\n",
    )
    with pytest.raises(ManifestInventoryError, match="not registered by apex_add_data_manifest"):
        inventory([tmp_path])


def test_hash_inside_a_quoted_argument_is_not_a_comment(tmp_path):
    """Verify a `#` within a quoted argument elsewhere on the line does not hide the call."""
    manifest = _component_tree(tmp_path, 'component = "Wave"')
    _write(
        tmp_path / "comp" / "CMakeLists.txt",
        'set(NOTE "tag#1")\napex_add_data_manifest() # registers the default\n',
    )
    assert inventory([tmp_path]) == [manifest]


def test_call_spanning_lines_is_read(tmp_path):
    """Verify a MANIFEST argument on its own line is read like a one-line call."""
    manifest = _write(tmp_path / "wave" / "wave_data.toml", 'component = "Wave"\n')
    _write(
        tmp_path / "wave" / "CMakeLists.txt",
        'apex_add_data_manifest(\n  MANIFEST\n  "${CMAKE_CURRENT_SOURCE_DIR}/wave_data.toml"\n)\n',
    )
    assert inventory([tmp_path]) == [manifest]


@pytest.mark.parametrize(
    "arguments",
    ["MANIFEST", "MANIFEST a b", 'FOO "x"', 'MANIFEST "${OTHER_DIR}/x_data.toml"'],
)
def test_argument_shapes_outside_the_contract_fail(tmp_path, arguments):
    """Verify a call the reader cannot interpret is an error, never a silent default."""
    _component_tree(tmp_path, 'component = "Wave"')
    _write(tmp_path / "odd" / "CMakeLists.txt", f"apex_add_data_manifest({arguments})\n")
    with pytest.raises(ManifestInventoryError, match="not a form this inventory|cannot resolve"):
        inventory([tmp_path])
