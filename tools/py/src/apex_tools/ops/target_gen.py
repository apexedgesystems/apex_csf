"""Generate Zenith target configuration from app_data.toml and struct dictionaries.

Reads an app_data.toml (per-application component manifest) and the JSON struct
dictionaries produced by apex_data_gen, then outputs a complete Zenith target
directory:

  app_manifest.json   - Component list + protocol config
  commands.json       - Opcode table for command panel
  telemetry.json      - Default telemetry plot layouts
  structs/*.json      - Struct dictionaries (copied from apex_data_db)

Usage:
  zenith-target --app ApexOpsDemo --apps-dir apps \
    --db build/apex_data_db --output build/zenith_targets/ApexOpsDemo
  zenith-target --app-data apps/apex_ops_demo/app_data.toml --db build/apex_data_db --output out/
"""

import argparse
import json
import shutil
import sys

if sys.version_info >= (3, 11):
    import tomllib
else:
    try:
        import tomllib
    except ModuleNotFoundError:
        import tomli as tomllib  # type: ignore[no-redef]

from pathlib import Path

# ---------------------------------------------------------------------------
# Core component definitions (auto-included in every Apex application)
# ---------------------------------------------------------------------------

CORE_COMPONENTS = [
    {
        "name": "Executive",
        "fullUid": "0x000000",
        "type": "EXECUTIVE",
        "dataFile": "ApexExecutive.json",
        "notes": "System executive. Health via GET_HEALTH (opcode 0x0100).",
    },
    {
        "name": "Scheduler",
        "fullUid": "0x000100",
        "type": "CORE",
        "dataFile": "Scheduler.json",
        "notes": "Task scheduler. Health via GET_HEALTH (opcode 0x0100).",
    },
    {
        "name": "FileSystem",
        "fullUid": "0x000200",
        "type": "CORE",
        "notes": "File I/O for TPRM loading, file transfers, and library management.",
    },
    {
        "name": "Registry",
        "fullUid": "0x000300",
        "type": "CORE",
        "notes": "Component and data registry. Handles INSPECT queries.",
    },
    {
        "name": "Interface",
        "fullUid": "0x000400",
        "type": "CORE",
        "dataFile": "ApexInterface.json",
        "notes": "TCP/SLIP interface. Stats via GET_STATS (opcode 0x0100).",
    },
    {
        "name": "Action",
        "fullUid": "0x000500",
        "type": "CORE",
        "dataFile": "ActionComponent.json",
        "notes": "Sequence engine. Stats via GET_STATS (opcode 0x0100).",
    },
]

# System opcodes handled by ApexInterface (universal across all apps)
SYSTEM_OPCODES = {
    "0x0000": "SYS_NOOP - connectivity check (any fullUid)",
    "0x0001": "SYS_PING - echo payload back",
    "0x0020": "FILE_BEGIN - start file upload",
    "0x0021": "FILE_CHUNK - file data chunk",
    "0x0022": "FILE_END - finalize upload",
    "0x0023": "FILE_ABORT - abort transfer",
    "0x0024": "FILE_STATUS - query transfer status",
    "0x0025": "FILE_GET - request file download",
    "0x0026": "FILE_READ_CHUNK - read chunk from target",
}

BASE_COMPONENT_OPCODES = {
    "0x0080": "GET_COMMAND_COUNT - command statistics (all components)",
    "0x0081": "GET_STATUS_INFO - component lifecycle status (all components)",
    "0x0100": "GET_HEALTH/GET_STATS - component-specific health telemetry",
}

INSPECT_CATEGORIES = {
    "0": "STATIC_PARAM - read-only constants",
    "1": "TUNABLE_PARAM - runtime-adjustable parameters",
    "2": "STATE - internal component state",
    "3": "INPUT - external data fed to component",
    "4": "OUTPUT - data produced by component",
}

# Executive opcode field definitions (for commands.json)
EXECUTIVE_FIELD_DEFS = {
    "CMD_LOCK_COMPONENT": [{"name": "fullUid", "type": "uint32", "desc": "Component to lock"}],
    "CMD_UNLOCK_COMPONENT": [{"name": "fullUid", "type": "uint32", "desc": "Component to unlock"}],
    "SET_VERBOSITY": [
        {
            "name": "level",
            "type": "uint8",
            "desc": "0=OFF 1=ERROR 2=WARN 3=INFO 4=DEBUG",
            "default": 3,
        }
    ],
    "RELOAD_TPRM": [{"name": "fullUid", "type": "uint32", "desc": "Target component UID"}],
    "RELOAD_LIBRARY": [{"name": "fullUid", "type": "uint32", "desc": "Target component UID"}],
    "INSPECT": [
        {"name": "fullUid", "type": "uint32", "desc": "Component UID"},
        {
            "name": "category",
            "type": "uint8",
            "desc": "0=STATIC 1=TUNABLE 2=STATE 3=INPUT 4=OUTPUT",
        },
        {"name": "offset", "type": "uint16", "desc": "Byte offset", "default": 0},
        {"name": "length", "type": "uint16", "desc": "Byte count (0=all)", "default": 0},
    ],
}

QUICK_COMMANDS = [
    {"label": "NOOP", "fullUid": "0x000000", "opcode": "0x0000", "desc": "Connectivity check"},
    {"label": "Ping", "fullUid": "0x000000", "opcode": "0x0001", "desc": "Echo test"},
    {"label": "Pause", "fullUid": "0x000000", "opcode": "0x0110", "desc": "Pause execution"},
    {"label": "Resume", "fullUid": "0x000000", "opcode": "0x0111", "desc": "Resume execution"},
    {"label": "Sleep", "fullUid": "0x000000", "opcode": "0x0116", "desc": "Enter sleep mode"},
    {"label": "Wake", "fullUid": "0x000000", "opcode": "0x0117", "desc": "Exit sleep mode"},
]


# ---------------------------------------------------------------------------
# Loader helpers
# ---------------------------------------------------------------------------


def load_app_data(path: str) -> dict:
    """Load and validate app_data.toml."""
    with open(path, "rb") as f:
        data = tomllib.load(f)

    if "application" not in data:
        raise ValueError(f"Missing 'application' field in {path}")
    if "protocol" not in data:
        raise ValueError(f"Missing [protocol] section in {path}")
    if "components" not in data:
        raise ValueError(f"Missing [[components]] entries in {path}")

    return data


def load_struct_dicts(db_dir: str) -> dict:
    """Load all JSON struct dictionaries from a directory. Returns {filename: data}."""
    dicts = {}
    db_path = Path(db_dir)
    if not db_path.exists():
        return dicts
    for f in sorted(db_path.glob("*.json")):
        with open(f) as fh:
            dicts[f.name] = json.load(fh)
    return dicts


def find_struct_dict(component_name: str, struct_dicts: dict) -> tuple:
    """Find struct dict JSON for a component by name.

    Returns (filename, data) or (None, None).
    """
    # Direct match: ComponentName.json
    direct = f"{component_name}.json"
    if direct in struct_dicts:
        return direct, struct_dicts[direct]

    # Search by component field inside JSON
    for fname, data in struct_dicts.items():
        if data.get("component") == component_name:
            return fname, data

    return None, None


# ---------------------------------------------------------------------------
# Generators
# ---------------------------------------------------------------------------


def generate_app_manifest(app_data: dict, struct_dicts: dict) -> dict:
    """Generate app_manifest.json from app_data.toml + struct dicts."""
    protocol = app_data["protocol"]

    manifest = {
        "application": app_data["application"],
        "description": app_data.get("description", ""),
        "protocol": {
            "transport": protocol.get("transport", "TCP"),
            "framing": protocol.get("framing", "SLIP"),
            "port": protocol.get("port", 9000),
            "header": "AprotoProtocol.json",
        },
        "components": [],
        "systemOpcodes": {
            "description": "System opcodes (0x0000-0x00FF) handled by ApexInterface.",
            "interface": SYSTEM_OPCODES,
            "component": BASE_COMPONENT_OPCODES,
        },
        "inspectCategories": INSPECT_CATEGORIES,
    }

    # Add core components
    for core in CORE_COMPONENTS:
        entry = dict(core)
        manifest["components"].append(entry)

    # Add app-specific components
    for comp in app_data["components"]:
        entry = {
            "name": comp["name"],
            "fullUid": comp["fullUid"],
            "type": comp.get("type", "SW_MODEL"),
        }
        if "instanceIndex" in comp:
            entry["instanceIndex"] = comp["instanceIndex"]
        if "notes" in comp:
            entry["notes"] = comp["notes"]

        # Find matching struct dict
        data_file, _ = find_struct_dict(comp["name"], struct_dicts)
        if data_file:
            entry["dataFile"] = data_file

        manifest["components"].append(entry)

    return manifest


def generate_commands(app_data: dict, struct_dicts: dict) -> dict:
    """Generate commands.json from struct dict enums + universal opcodes."""
    commands: dict = {"quickCommands": QUICK_COMMANDS, "components": {}}

    # Executive commands (from ApexExecutive.json enums)
    exec_dict = struct_dicts.get("ApexExecutive.json", {})
    exec_enums = exec_dict.get("enums", {}).get("Opcode", {}).get("values", {})

    exec_cmds: list[dict] = []
    # System opcodes available on executive
    exec_cmds.append(
        {"name": "NOOP", "opcode": "0x0000", "desc": "Connectivity check", "fields": []}
    )
    exec_cmds.append({"name": "PING", "opcode": "0x0001", "desc": "Echo payload", "fields": []})

    # Sort executive opcodes by value for consistent ordering
    for name, value in sorted(exec_enums.items(), key=lambda x: x[1]):
        opcode = f"0x{value:04x}"
        fields = EXECUTIVE_FIELD_DEFS.get(name, [])
        desc = _opcode_desc(name)
        exec_cmds.append({"name": name, "opcode": opcode, "desc": desc, "fields": fields})

    commands["components"]["Executive"] = {"fullUid": "0x000000", "commands": exec_cmds}

    # Per-component commands
    seen = set()
    for comp in _all_components(app_data):
        name = comp["name"]
        uid = comp["fullUid"]
        label = _component_label(comp)

        if label in seen:
            continue
        seen.add(label)

        # Skip executive (already handled above)
        if name in ("Executive",):
            continue

        comp_cmds = [
            {"name": "NOOP", "opcode": "0x0000", "desc": "Connectivity check", "fields": []},
        ]

        # Find component-specific opcodes from struct dict enums
        _data_file, data = find_struct_dict(name, struct_dicts)
        if data:
            for _enum_name, enum_data in data.get("enums", {}).items():
                for op_name, op_value in sorted(
                    enum_data.get("values", {}).items(), key=lambda x: x[1]
                ):
                    comp_cmds.append(
                        {
                            "name": op_name,
                            "opcode": f"0x{op_value:04x}",
                            "desc": _opcode_desc(op_name),
                            "fields": [],
                        }
                    )

        # If no component-specific opcodes, add GET_HEALTH as default
        if len(comp_cmds) == 1:
            comp_cmds.append(
                {
                    "name": "GET_HEALTH",
                    "opcode": "0x0100",
                    "desc": "Component health telemetry",
                    "fields": [],
                }
            )

        commands["components"][label] = {"fullUid": uid, "commands": comp_cmds}

    return commands


def generate_telemetry(app_data: dict, struct_dicts: dict) -> dict:
    """Generate telemetry.json with default plot layouts from OUTPUT structs."""
    layouts = []

    # Collect OUTPUT channels per component instance
    for comp in app_data["components"]:
        name = comp["name"]
        idx = comp.get("instanceIndex", 0)
        prefix = f"{name}#{idx}" if _has_multi_instance(name, app_data) else name

        _, data = find_struct_dict(name, struct_dicts)
        if not data:
            continue

        # Find OUTPUT or TELEMETRY-category structs (both produce plottable data)
        for _struct_name, struct_info in data.get("structs", {}).items():
            cat = struct_info.get("category", "")
            if cat not in ("OUTPUT", "TELEMETRY"):
                continue

            channels = []
            for field in struct_info.get("fields", []):
                ftype = field.get("type", "")
                fname = field.get("name", "")
                # Skip padding, reserved, and non-numeric fields
                if fname in ("reserved", "pad0", "pad1", "pad2", "pad3"):
                    continue
                if ftype in ("float", "int", "uint"):
                    channels.append(f"{prefix}.{fname}")
                elif ftype == "array" and field.get("element_type") in ("float", "int", "uint"):
                    # For arrays, add first few elements as individual channels
                    dims = field.get("dims", [1])
                    count = min(dims[0], 4) if dims else 1
                    if count == 1:
                        channels.append(f"{prefix}.{fname}")
                    else:
                        for i in range(count):
                            channels.append(f"{prefix}.{fname}[{i}]")

            if channels:
                plot: dict = {
                    "title": f"{prefix} Output",
                    "channels": channels,
                    "height": 200,
                }

                # Add thresholds for known patterns
                if name == "SystemMonitor":
                    plot = _sysmon_plots(prefix, struct_info)
                    if isinstance(plot, list):
                        layouts.extend(plot)
                        continue

                layouts.append(plot)

    if not layouts:
        return {"layouts": []}

    # Group into a single default layout
    return {
        "layouts": [
            {"name": "Default", "plots": layouts},
        ]
    }


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _all_components(app_data: dict) -> list:
    """Return core + app components."""
    result = []
    for core in CORE_COMPONENTS:
        result.append(core)
    for comp in app_data["components"]:
        result.append(comp)
    return result


def _component_label(comp: dict) -> str:
    """Build display label for a component (with instance index if multi-instance)."""
    name = comp["name"]
    if "instanceIndex" in comp:
        return f"{name} #{comp['instanceIndex']}"
    return name


def _has_multi_instance(name: str, app_data: dict) -> bool:
    """Check if a component name appears more than once in the app."""
    count = sum(1 for c in app_data["components"] if c["name"] == name)
    return count > 1


def _opcode_desc(name: str) -> str:
    """Generate a human-readable description from an opcode name."""
    desc_map = {
        "GET_HEALTH": "Executive health (48B response)",
        "EXEC_NOOP": "Executive connectivity check",
        "GET_CLOCK_FREQ": "Clock frequency (2B response)",
        "GET_RT_MODE": "RT mode (1B response)",
        "GET_CLOCK_CYCLES": "Cycle count (8B response)",
        "CMD_PAUSE": "Pause execution",
        "CMD_RESUME": "Resume execution",
        "CMD_SHUTDOWN": "Graceful shutdown",
        "CMD_FAST_FORWARD": "Enter fast-forward mode (non-RT)",
        "CMD_LOCK_COMPONENT": "Lock component",
        "CMD_UNLOCK_COMPONENT": "Unlock component",
        "CMD_SLEEP": "Enter sleep mode",
        "CMD_WAKE": "Exit sleep mode",
        "SET_VERBOSITY": "Set log verbosity level",
        "RELOAD_TPRM": "Reload component parameters",
        "RELOAD_LIBRARY": "Hot-swap shared library",
        "RELOAD_EXECUTIVE": "Restart via execve (destructive)",
        "INSPECT": "Read registered data block",
        "GET_REGISTRY": "Dump component registry (44B/entry)",
        "GET_DATA_CATALOG": "Dump data entry catalog (44B/entry)",
    }
    return desc_map.get(name, name.replace("_", " ").title())


def _sysmon_plots(prefix: str, struct_info: dict) -> list:
    """Generate SystemMonitor-specific plots with thresholds."""
    plots = []

    # CPU Temperature
    plots.append(
        {
            "title": "CPU Temperature",
            "channels": [f"{prefix}.cpuTempC"],
            "height": 180,
            "y_min": 0,
            "y_max": 100,
            "y_label": "C",
            "thresholds": [
                {"value": 75, "color": "#d29922", "label": "warn"},
                {"value": 85, "color": "#f85149", "label": "crit"},
            ],
        }
    )

    # RAM Utilization
    plots.append(
        {
            "title": "RAM Utilization",
            "channels": [f"{prefix}.ramUsedPercent"],
            "height": 160,
            "y_min": 0,
            "y_max": 100,
            "y_label": "%",
            "thresholds": [
                {"value": 80, "color": "#d29922", "label": "warn"},
                {"value": 95, "color": "#f85149", "label": "crit"},
            ],
        }
    )

    # File Descriptors
    plots.append(
        {
            "title": "Open File Descriptors",
            "channels": [f"{prefix}.fdCount"],
            "height": 140,
            "y_label": "count",
        }
    )

    return plots


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def find_app_data(app_name: str, apps_dir: str) -> str:
    """Search apps/ for app_data.toml matching the application name."""
    for entry in sorted(Path(apps_dir).iterdir()):
        candidate = entry / "app_data.toml"
        if candidate.exists():
            with open(candidate, "rb") as f:
                data = tomllib.load(f)
            if data.get("application") == app_name:
                return str(candidate)
    raise FileNotFoundError(f"No app_data.toml with application='{app_name}' found in {apps_dir}/")


def main():
    parser = argparse.ArgumentParser(
        description="Generate Zenith target configuration from app_data.toml"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--app", help="Application name (searches apps/ for app_data.toml)")
    group.add_argument("--app-data", help="Direct path to app_data.toml")

    parser.add_argument("--apps-dir", default="apps", help="Apps directory (default: apps)")
    parser.add_argument("--db", required=True, help="Path to apex_data_db/ directory")
    parser.add_argument("--output", required=True, help="Output directory for target configs")
    args = parser.parse_args()

    # Find app_data.toml
    if args.app_data:
        app_data_path = args.app_data
    else:
        app_data_path = find_app_data(args.app, args.apps_dir)

    # Load inputs
    app_data = load_app_data(app_data_path)
    struct_dicts = load_struct_dicts(args.db)

    app_name = app_data["application"]
    print(f"[zenith-target] Generating target configs for {app_name}")
    print(f"[zenith-target] Struct dicts: {len(struct_dicts)} files from {args.db}")

    # Generate
    manifest = generate_app_manifest(app_data, struct_dicts)
    commands = generate_commands(app_data, struct_dicts)
    telemetry = generate_telemetry(app_data, struct_dicts)

    # Write output
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    with open(out / "app_manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    count = len(manifest["components"])
    print(f"[zenith-target] {out / 'app_manifest.json'} ({count} components)")

    with open(out / "commands.json", "w") as f:
        json.dump(commands, f, indent=2)
        f.write("\n")
    nsections = len(commands["components"])
    print(f"[zenith-target] {out / 'commands.json'} ({nsections} sections)")

    with open(out / "telemetry.json", "w") as f:
        json.dump(telemetry, f, indent=2)
        f.write("\n")
    plot_count = sum(len(layout.get("plots", [])) for layout in telemetry.get("layouts", []))
    print(f"[zenith-target] {out / 'telemetry.json'} ({plot_count} plots)")

    # Copy struct dicts
    structs_dir = out / "structs"
    structs_dir.mkdir(exist_ok=True)
    for fname in sorted(struct_dicts.keys()):
        src = Path(args.db) / fname
        shutil.copy2(src, structs_dir / fname)
    print(f"[zenith-target] {structs_dir}/ ({len(struct_dicts)} files)")

    print(f"[zenith-target] Done. Output: {out}/")


if __name__ == "__main__":
    main()
