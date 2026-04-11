"""Unit tests for apex_tools.ops.deck_gen module.

Tests struct dictionary loading, category grouping, and field formatting.
"""

import json

from apex_tools.ops.deck_gen import (
    collect_by_category,
    format_type,
    format_value,
    load_dicts,
)

# =============================== Format Helpers ================================


def test_format_type_simple():
    """format_type renders a simple scalar type."""
    field = {"type": "uint32_t"}
    result = format_type(field)
    assert result == "uint32_t"


def test_format_type_array():
    """format_type renders array types with element type and dimensions."""
    field = {"type": "array", "element_type": "uint8_t", "dims": [16]}
    result = format_type(field)
    assert "uint8_t" in result
    assert "16" in result


def test_format_value_int():
    """format_value renders integer values."""
    result = format_value(42)
    assert "42" in str(result)


def test_format_value_float():
    """format_value renders float values."""
    result = format_value(3.14)
    assert "3.14" in str(result)


# =============================== Dictionary Loading ================================


def test_load_dicts_reads_json(tmp_path):
    """load_dicts reads JSON struct dictionaries from a directory."""
    data = {"component": "TestComp", "structs": {}}
    (tmp_path / "TestComp.json").write_text(json.dumps(data))
    result = load_dicts(str(tmp_path))
    assert len(result) == 1
    assert result[0]["component"] == "TestComp"


# =============================== Category Grouping ================================


def test_collect_by_category_groups_correctly():
    """collect_by_category returns tuple of (commands, telemetry, tunables, states, protocol)."""
    dicts = [
        {
            "component": "Comp",
            "structs": {
                "Params": {
                    "category": "TUNABLE_PARAM",
                    "fields": [],
                },
                "State": {
                    "category": "STATE",
                    "fields": [],
                },
                "Cmd": {
                    "category": "COMMAND",
                    "fields": [],
                },
            },
        }
    ]
    commands, telemetry, tunables, states, protocol = collect_by_category(dicts)
    assert len(tunables) == 1
    assert len(states) == 1
    assert len(commands) == 1
    assert tunables[0]["struct"] == "Params"


def test_collect_by_category_empty():
    """collect_by_category handles empty input."""
    commands, telemetry, tunables, states, protocol = collect_by_category([])
    assert commands == []
    assert telemetry == []
    assert tunables == []
    assert states == []
    assert protocol == []
