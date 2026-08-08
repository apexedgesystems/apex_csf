"""Tests for Zenith target generation from app_data + struct dictionaries.

Covers the spec-declared command path: components whose dictionary
carries a `commands` array get their command panel from it (fields
derived from the request struct), while enum-based components keep the
legacy path.
"""

from apex_tools.ops.target_gen import generate_commands

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
