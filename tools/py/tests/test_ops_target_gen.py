"""Tests for Zenith target generation from app_data + struct dictionaries.

Covers the spec-declared command path: components whose dictionary
carries a `commands` array get their command panel from it (fields
derived from the request struct), while enum-based components keep the
legacy path. Also covers app-declared quick commands and layouts.
"""

from apex_tools.ops.target_gen import (
    QUICK_COMMANDS,
    expand_array_fields,
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
        {"name": "ValueComp", "fullUid": "0x00E200", "type": "SW_MODEL"},
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

# A component whose only enum is a plain value enum, as the rover's
# controller has for its drive mode. Its values sit in the protocol's own
# opcode range, so they are not this component's commands.
VALUE_ENUM_DICT = {
    "component": "ValueComp",
    "structs": {},
    "enums": {
        "DriveMode": {
            "underlying_type": "uint8_t",
            "values": {"HOLD": 0, "TRAJECTORY": 1, "WAYPOINT": 2},
        }
    },
}

STRUCT_DICTS = {
    "SpecSensor.json": SPEC_DICT,
    "EnumComp.json": ENUM_DICT,
    "ValueComp.json": VALUE_ENUM_DICT,
}


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


def test_value_enums_are_not_exported_as_commands():
    """A mode enum is not a command surface.

    Its values fall in 0x0000..0x00FF, which the interface answers itself
    as NOOP / PING / GET_STATUS. Exported as commands they would appear
    on the console under the mode's name, acknowledge, and leave the
    component untouched.
    """
    commands = generate_commands(APP_DATA, STRUCT_DICTS)
    cmds = _component_commands(commands, "ValueComp")
    names = [c["name"] for c in cmds]
    # NOOP and nothing else: no health command is assumed for a component
    # that does not implement one.
    assert names == ["NOOP"]
    assert "HOLD" not in names
    assert "WAYPOINT" not in names


def test_enum_fallback_still_generates():
    commands = generate_commands(APP_DATA, STRUCT_DICTS)
    cmds = _component_commands(commands, "EnumComp")
    names = [c["name"] for c in cmds]
    assert names == ["NOOP", "GET_STATS"]
    assert cmds[1]["opcode"] == "0x0100"


def test_app_quick_commands_follow_the_universal_ones():
    """App quick commands append after the universal set, keeping payloadHex only when given."""
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
    """An app that declares no quick commands gets exactly the universal set."""
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
    """App-declared layouts lead the list, with plot defaults filled in, ahead of Default."""
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
    """With no app layouts and no OUTPUT structs, no layout (not even Default) is emitted."""
    assert generate_telemetry(APP_DATA, {}) == {"layouts": []}


def test_find_app_data_searches_nested_apps(tmp_path):
    """find_app_data locates apps at any depth under each root and tolerates a missing root."""
    top = tmp_path / "demos" / "top_app"
    nested = tmp_path / "demos" / "family" / "nested_app"
    for d, name in ((top, "TopApp"), (nested, "NestedApp")):
        d.mkdir(parents=True)
        (d / "app_data.toml").write_text(f'application = "{name}"\n')
    roots = f"{tmp_path / 'demos'}:{tmp_path / 'missing'}"
    assert find_app_data("TopApp", roots) == str(top / "app_data.toml")
    assert find_app_data("NestedApp", roots) == str(nested / "app_data.toml")


def test_generated_default_plots_named_scalars_only():
    """The Default layout plots named scalars only, never arrays, reserved or padding fields."""
    dicts = {
        "EnumComp.json": {
            "component": "EnumComp",
            "structs": {
                "EnumCompOutput": {
                    "category": "OUTPUT",
                    "fields": [
                        {"name": "speed", "type": "float", "size": 8},
                        {"name": "ranges", "type": "array", "element_type": "float", "dims": [8]},
                        {
                            "name": "reserved1",
                            "type": "array",
                            "element_type": "uint",
                            "dims": [24],
                        },
                        {"name": "reserved0", "type": "uint", "size": 2},
                        {"name": "pad_a", "type": "uint", "size": 1},
                        {"name": "mode", "type": "uint", "size": 1},
                    ],
                }
            },
        }
    }
    layouts = generate_telemetry(APP_DATA, dicts)["layouts"]
    assert layouts[0]["plots"][0]["channels"] == ["EnumComp.speed", "EnumComp.mode"]


# ---------------------------------------------------------------------------
# Array expansion: a short numeric array's elements become scalar fields
# ---------------------------------------------------------------------------

RAY_DICT = {
    "component": "Fan",
    "structs": {
        "FanOutput": {
            "category": "OUTPUT",
            "size": 112,
            "fields": [
                {"name": "n_rays", "type": "uint", "offset": 0, "size": 4},
                {
                    "name": "pad0",
                    "type": "array",
                    "offset": 4,
                    "size": 4,
                    "element_type": "uint",
                    "dims": [4],
                },
                {
                    "name": "range_m",
                    "type": "array",
                    "offset": 8,
                    "size": 32,
                    "element_type": "float",
                    "dims": [4],
                },
                {
                    "name": "hit",
                    "type": "array",
                    "offset": 40,
                    "size": 4,
                    "element_type": "uint",
                    "dims": [4],
                },
                {
                    "name": "reserved1",
                    "type": "array",
                    "offset": 44,
                    "size": 4,
                    "element_type": "uint",
                    "dims": [4],
                },
                {
                    "name": "big",
                    "type": "array",
                    "offset": 48,
                    "size": 64,
                    "element_type": "uint",
                    "dims": [64],
                },
            ],
        }
    },
}


def _field_names(dicts: dict) -> list:
    return [f["name"] for f in dicts["Fan.json"]["structs"]["FanOutput"]["fields"]]


def test_short_numeric_arrays_expand_into_element_fields():
    """Each element gets a scalar field at the array's offset plus its index."""
    out = expand_array_fields({"Fan.json": RAY_DICT})
    fields = {f["name"]: f for f in out["Fan.json"]["structs"]["FanOutput"]["fields"]}
    assert fields["range_m[0]"] == {
        "name": "range_m[0]",
        "type": "float",
        "offset": 8,
        "size": 8,
        "value": 0,
        "doc": "Element 0 of range_m.",
    }
    assert fields["range_m[3]"]["offset"] == 32
    assert fields["hit[2]"] == {
        "name": "hit[2]",
        "type": "uint",
        "offset": 42,
        "size": 1,
        "value": 0,
        "doc": "Element 2 of hit.",
    }
    # The array entries themselves stay for consumers that read arrays.
    assert fields["range_m"]["type"] == "array"


def test_reserved_padding_and_long_arrays_are_not_expanded():
    """Padding and reserved arrays are not channels; a 64-element array is a buffer."""
    names = _field_names(expand_array_fields({"Fan.json": RAY_DICT}))
    assert "pad0[0]" not in names
    assert "reserved1[0]" not in names
    assert "big[0]" not in names
    assert "n_rays" in names


def test_expansion_is_idempotent_and_leaves_the_input_alone():
    """Expanding twice adds nothing, and the input dictionaries are not mutated."""
    once = expand_array_fields({"Fan.json": RAY_DICT})
    twice = expand_array_fields(once)
    assert _field_names(twice) == _field_names(once)
    assert "range_m[0]" not in [f["name"] for f in RAY_DICT["structs"]["FanOutput"]["fields"]]


def test_generated_default_layout_skips_array_elements():
    """The catch-all plots the scalars; the app's own layouts may name the rays."""
    app = dict(APP_DATA)
    app["components"] = [{"name": "Fan", "fullUid": "0x00D900", "type": "SW_MODEL"}]
    dicts = expand_array_fields({"Fan.json": RAY_DICT})
    layouts = generate_telemetry(app, dicts)["layouts"]
    default = next(lay for lay in layouts if lay["name"] == "Default")
    channels = [c for plot in default["plots"] for c in plot["channels"]]
    assert channels == ["Fan.n_rays"]


# ---------------------------------------------------------------------------
# The uploader's running-id domain is the plant's: bit 0x10 clear, one byte
# ---------------------------------------------------------------------------


def _upload_rts_module():
    import importlib.util
    import pathlib

    path = (
        pathlib.Path(__file__).resolve().parents[3]
        / "demos/apex_horizon_demo/rover_mcu/scripts/upload_rts.py"
    )
    spec = importlib.util.spec_from_file_location("upload_rts", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_uploader_running_id_domain_is_the_plants():
    """A running id is a byte with bit 0x10 clear: 16..31 and 48..63 are out, 47 and 64 in."""
    mod = _upload_rts_module()
    assert all(mod.running_id_expressible(i) for i in (1, 9, 15, 32, 36, 47, 64, 79, 207))
    # 255 is 0xFF: it carries the halt bit like every value in 16..31, 48..63, ...
    assert not any(mod.running_id_expressible(i) for i in (0, 16, 22, 31, 48, 63, 255, 256, 300))


# ---------------------------------------------------------------------------
# The uploader's exit status follows each terminal command result
# ---------------------------------------------------------------------------


def _run_uploader(monkeypatch, tmp_path, rescan_status, start_status):
    """Run the real uploader main with compiler, transfer and client stubbed.

    Returns (exit status, opcodes sent, stdout, stderr). No socket or process
    is created: the compiler stub checks that the uploader runs the build-role
    cfg2bin the tree installs, with the config, the output and the spec pin,
    then writes a placeholder file; the client returns the given terminal
    statuses.
    """
    import contextlib
    import io
    import pathlib
    import sys
    import types

    mod = _upload_rts_module()
    names = {0: "SUCCESS", 4: "TARGET_NOT_FOUND", 5: "LOAD_FAILED"}
    sent = []
    toml = mod.MANIFEST.parent / "upload/rts_008_uploaded_beacon.toml"

    def compiler(argv, *, check):
        assert check is True
        assert pathlib.Path(argv[0]) == tmp_path / "bin/tools/build/cfg2bin", argv[0]
        assert argv[1:3] == ["--config", str(toml)]
        assert argv[3] == "--output"
        assert argv[5:] == ["--pin-spec", f"{mod.SPEC}:StandaloneSequenceTprm"]
        pathlib.Path(argv[4]).write_bytes(b"placeholder")
        return types.SimpleNamespace(returncode=0)

    class Client:
        def __init__(self, host, port, timeout):
            pass

        def connect(self):
            pass

        def send_file(self, local_path, remote_path):
            size = pathlib.Path(local_path).stat().st_size
            return {"status": 0, "file_end": {"bytes_written": size}}

        def send_command(self, uid, opcode, payload=b""):
            sent.append(opcode)
            status = rescan_status if opcode == mod.OP_RESCAN_CATALOG else start_status
            return {"status": status, "status_name": names[status], "queued": False}

    client_module = types.ModuleType("apex_tools.ops.client")
    client_module.AprotoClient = Client
    monkeypatch.setitem(sys.modules, "apex_tools.ops.client", client_module)
    monkeypatch.setattr(mod, "subprocess", types.SimpleNamespace(run=compiler))
    argv = ["upload_rts.py", str(toml), "--start", "--build-dir", str(tmp_path)]
    monkeypatch.setattr(sys, "argv", argv)
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = mod.main()
    return status, sent, out.getvalue(), err.getvalue()


def test_uploader_rejected_rescan_fails_before_start(monkeypatch, tmp_path):
    """A failed RESCAN_CATALOG exits nonzero, names the status, and never sends START."""
    status, sent, _, err = _run_uploader(monkeypatch, tmp_path, 4, 0)
    mod = _upload_rts_module()
    assert status != 0
    assert sent == [mod.OP_RESCAN_CATALOG]
    assert "RESCAN_CATALOG failed: TARGET_NOT_FOUND (code 4)" in err


def test_uploader_rejected_start_exits_nonzero(monkeypatch, tmp_path):
    """A failed START_RTS_BY_ID after a good rescan exits nonzero and names the status."""
    status, sent, _, err = _run_uploader(monkeypatch, tmp_path, 0, 5)
    mod = _upload_rts_module()
    assert status != 0
    assert sent == [mod.OP_RESCAN_CATALOG, mod.OP_START_RTS_BY_ID]
    assert "START_RTS_BY_ID 8 failed: LOAD_FAILED (code 5)" in err


def test_uploader_success_exits_zero(monkeypatch, tmp_path):
    """Transfer, rescan and start all succeeding exit zero with both statuses printed."""
    status, sent, out, err = _run_uploader(monkeypatch, tmp_path, 0, 0)
    mod = _upload_rts_module()
    assert status == 0
    assert sent == [mod.OP_RESCAN_CATALOG, mod.OP_START_RTS_BY_ID]
    assert "RESCAN_CATALOG: SUCCESS" in out and "START_RTS_BY_ID 8: SUCCESS" in out
    assert err == ""
