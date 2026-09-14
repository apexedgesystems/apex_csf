"""Tests for Zenith target generation from app_data + struct dictionaries.

Covers the spec-declared command path: components whose dictionary
carries a `commands` array get their command panel from it (fields
derived from the request struct), while enum-based components keep the
legacy path. Also covers app-declared quick commands and layouts.
"""

from apex_tools.ops.target_gen import (
    QUICK_COMMANDS,
    find_app_data,
    generate_commands,
    generate_telemetry,
)

APP_DATA = {
    "application": "TestApp",
    "protocol": {"transport": "TCP", "framing": "SLIP", "port": 9000},
    "components": [
        {"name": "SpecSensor", "fullUid": "0x00D400", "instanceIndex": 0, "type": "SW_MODEL"},
        {"name": "EnumComp", "fullUid": "0x00D500", "type": "SW_MODEL"},
    ],
}

SPEC_DICT = {
    "component": "SpecSensor",
    "structs": {
        "SetModeRequest": {
            "category": "COMMAND",
            "opcode": "0x0200",
            "size": 1,
            "fields": [
                {"name": "mode", "type": "uint", "offset": 0, "size": 1, "doc": "Requested mode."}
            ],
        },
        "RecalibrateRequest": {
            "category": "COMMAND",
            "opcode": "0x0201",
            "size": 4,
            "fields": [{"name": "referenceValue", "type": "float", "offset": 0, "size": 4}],
        },
    },
    "commands": [
        {
            "name": "SetMode",
            "opcode": "0x0200",
            "request": "SetModeRequest",
            "doc": "Select the operating mode.",
        },
        {
            "name": "Recalibrate",
            "opcode": "0x0201",
            "request": "RecalibrateRequest",
            "response": "StatsResponse",
        },
        {"name": "Reset", "opcode": "0x0203"},
    ],
}

ENUM_DICT = {
    "component": "EnumComp",
    "structs": {},
    "enums": {"EnumCompOpcode": {"underlying_type": "uint16_t", "values": {"GET_STATS": 256}}},
}

STRUCT_DICTS = {"SpecSensor.json": SPEC_DICT, "EnumComp.json": ENUM_DICT}


def _component_commands(commands: dict, label: str) -> list:
    return commands["components"][label]["commands"]


def test_spec_commands_are_authoritative():
    commands = generate_commands(APP_DATA, STRUCT_DICTS)
    cmds = _component_commands(commands, "SpecSensor #0")
    names = [c["name"] for c in cmds]
    assert names == ["NOOP", "SetMode", "Recalibrate", "Reset"]

    set_mode = cmds[1]
    assert set_mode["opcode"] == "0x0200"
    assert set_mode["desc"] == "Select the operating mode."
    assert set_mode["fields"] == [{"name": "mode", "type": "uint8", "desc": "Requested mode."}]

    recal = cmds[2]
    assert recal["fields"] == [{"name": "referenceValue", "type": "float", "desc": ""}]

    reset = cmds[3]
    assert reset["fields"] == []


def test_enum_fallback_still_generates():
    commands = generate_commands(APP_DATA, STRUCT_DICTS)
    cmds = _component_commands(commands, "EnumComp")
    names = [c["name"] for c in cmds]
    assert names == ["NOOP", "GET_STATS"]
    assert cmds[1]["opcode"] == "0x0100"


def test_app_quick_commands_follow_the_universal_ones():
    app = dict(APP_DATA)
    app["quickCommands"] = [
        {
            "label": "Run 1",
            "fullUid": "0x000500",
            "opcode": "0x0510",
            "payloadHex": "0100",
            "desc": "Start sequence 1",
        },
        {"label": "Noop A", "fullUid": "0x00D500", "opcode": "0x0000"},
    ]
    quick = generate_commands(app, STRUCT_DICTS)["quickCommands"]
    assert quick[: len(QUICK_COMMANDS)] == QUICK_COMMANDS
    assert quick[len(QUICK_COMMANDS)] == {
        "label": "Run 1",
        "fullUid": "0x000500",
        "opcode": "0x0510",
        "desc": "Start sequence 1",
        "payloadHex": "0100",
    }
    assert "payloadHex" not in quick[-1]


def test_without_app_quick_commands_the_list_is_unchanged():
    assert generate_commands(APP_DATA, STRUCT_DICTS)["quickCommands"] == QUICK_COMMANDS


OUTPUT_DICT = {
    "component": "EnumComp",
    "structs": {
        "EnumCompOutput": {
            "category": "OUTPUT",
            "fields": [{"name": "speed", "type": "float", "size": 8}],
        }
    },
}


def test_app_layouts_come_before_the_generated_default():
    app = dict(APP_DATA)
    app["layouts"] = [
        {"name": "Drive", "plots": [{"title": "Speed", "channels": ["EnumComp.speed"]}]},
        {"name": "Empty"},
    ]
    layouts = generate_telemetry(app, {"EnumComp.json": OUTPUT_DICT})["layouts"]
    assert [lay["name"] for lay in layouts] == ["Drive", "Empty", "Default"]
    assert layouts[0]["plots"] == [
        {"title": "Speed", "channels": ["EnumComp.speed"], "height": 200}
    ]
    assert layouts[1]["plots"] == []
    assert layouts[2]["plots"][0]["channels"] == ["EnumComp.speed"]


def test_without_layouts_or_outputs_telemetry_is_empty():
    assert generate_telemetry(APP_DATA, {}) == {"layouts": []}


def test_find_app_data_searches_nested_apps(tmp_path):
    top = tmp_path / "demos" / "top_app"
    nested = tmp_path / "demos" / "family" / "nested_app"
    for d, name in ((top, "TopApp"), (nested, "NestedApp")):
        d.mkdir(parents=True)
        (d / "app_data.toml").write_text(f'application = "{name}"\n')
    roots = f"{tmp_path / 'demos'}:{tmp_path / 'missing'}"
    assert find_app_data("TopApp", roots) == str(top / "app_data.toml")
    assert find_app_data("NestedApp", roots) == str(nested / "app_data.toml")
