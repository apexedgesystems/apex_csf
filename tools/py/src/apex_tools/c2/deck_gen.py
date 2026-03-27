"""Generate a consolidated command/telemetry deck from struct dictionaries.

Reads all JSON struct dictionaries produced by apex_data_gen and generates
a single markdown document describing the full C2 interface surface of an
Apex application.

Usage:
    c2-deck --db build/native-linux-debug/apex_data_db --output c2_deck.md
    c2-deck --db build/native-linux-debug/apex_data_db  # prints to stdout
"""

import argparse
import json
import sys
from pathlib import Path

SYSTEM_OPCODES = [
    (0x0000, "NOOP", "No-op, returns ACK"),
    (0x0001, "PING", "Echo payload back to sender"),
    (0x0002, "GET_STATUS", "Component status query"),
    (0x0003, "RESET", "Reset component to initial state"),
    (0x00FE, "ACK", "Positive acknowledgment"),
    (0x00FF, "NAK", "Negative acknowledgment"),
]


def load_dicts(db_dir):
    """Load all JSON struct dictionaries from a directory."""
    dicts = []
    db_path = Path(db_dir)
    if not db_path.is_dir():
        print(f"Error: {db_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    for json_file in sorted(db_path.glob("*.json")):
        with open(json_file) as f:
            data = json.load(f)
        data["_source"] = json_file.name
        dicts.append(data)

    if not dicts:
        print(f"Error: no JSON files found in {db_dir}", file=sys.stderr)
        sys.exit(1)

    return dicts


def collect_by_category(dicts):
    """Group structs across all components by category."""
    commands = []
    telemetry = []
    tunables = []
    states = []
    protocol = []

    for d in dicts:
        component = d.get("component", "Unknown")
        for struct_name, info in d.get("structs", {}).items():
            cat = info.get("category", "")
            entry = {
                "component": component,
                "struct": struct_name,
                "category": cat,
                "opcode": info.get("opcode"),
                "size": info.get("size", 0),
                "fields": info.get("fields", []),
            }
            if cat == "COMMAND":
                commands.append(entry)
            elif cat == "TELEMETRY":
                telemetry.append(entry)
            elif cat == "TUNABLE_PARAM":
                tunables.append(entry)
            elif cat == "STATE":
                states.append(entry)
            elif cat == "PROTOCOL":
                protocol.append(entry)

    return commands, telemetry, tunables, states, protocol


def format_type(field):
    """Format field type for display."""
    ftype = field.get("type", "?")
    if ftype == "array":
        elem = field.get("element_type", "?")
        dims = field.get("dims", [])
        if dims:
            return f"{elem}[{dims[0]}]"
        return f"{elem}[]"
    return ftype


def format_value(val):
    """Format default value for display."""
    if val is None:
        return "-"
    if isinstance(val, str) and "std::" in val:
        return "-"
    if isinstance(val, list):
        return "[]"
    return str(val)


def emit_field_table(fields, out):
    """Write a field layout table."""
    out.write("| Offset | Size | Field | Type | Default |\n")
    out.write("|--------|------|-------|------|---------|\n")
    for f in fields:
        ftype = format_type(f)
        val = format_value(f.get("value"))
        out.write(f"| {f['offset']} | {f['size']} | " f"`{f['name']}` | {ftype} | {val} |\n")
    out.write("\n")


def generate_deck(dicts, out):
    """Generate the full cmd/tlm deck markdown."""
    commands, telemetry, tunables, states, protocol = collect_by_category(dicts)
    components = sorted({d.get("component", "?") for d in dicts})

    out.write("# Command and Telemetry Deck\n\n")
    out.write("Auto-generated from struct dictionaries. " "Do not edit manually.\n\n")

    # Table of contents
    out.write("## Contents\n\n")
    out.write("1. [Components](#1-components)\n")
    out.write("2. [System Opcodes](#2-system-opcodes)\n")
    out.write("3. [Commands](#3-commands)\n")
    out.write("4. [Telemetry](#4-telemetry)\n")
    out.write("5. [Tunable Parameters](#5-tunable-parameters)\n")
    out.write("6. [State Data](#6-state-data)\n")
    out.write("7. [Wire Protocol](#7-wire-protocol)\n\n")
    out.write("---\n\n")

    # 1. Component summary
    out.write("## 1. Components\n\n")
    out.write("| Component | Commands | Telemetry | Tunables | State |\n")
    out.write("|-----------|----------|-----------|----------|-------|\n")
    for comp in components:
        n_cmd = sum(1 for e in commands if e["component"] == comp)
        n_tlm = sum(1 for e in telemetry if e["component"] == comp)
        n_tun = sum(1 for e in tunables if e["component"] == comp)
        n_st = sum(1 for e in states if e["component"] == comp)
        out.write(f"| {comp} | {n_cmd} | {n_tlm} | {n_tun} | {n_st} |\n")
    out.write("\n")

    # 2. System opcodes
    out.write("## 2. System Opcodes\n\n")
    out.write(
        "System opcodes (0x0000-0x00FF) are handled by all components "
        "via the base SystemComponent interface.\n\n"
    )
    out.write("| Opcode | Name | Description |\n")
    out.write("|--------|------|-------------|\n")
    for opcode, name, desc in SYSTEM_OPCODES:
        out.write(f"| 0x{opcode:04X} | {name} | {desc} |\n")
    out.write("\n")

    # 3. Commands
    out.write("## 3. Commands\n\n")
    if not commands:
        out.write("No component-specific commands defined.\n\n")
    else:
        out.write("### Command Summary\n\n")
        out.write("| Component | Opcode | Struct | Size |\n")
        out.write("|-----------|--------|--------|------|\n")
        for e in sorted(commands, key=lambda x: (x["component"], x["opcode"] or "")):
            opc = e["opcode"] or "-"
            out.write(f"| {e['component']} | {opc} | " f"`{e['struct']}` | {e['size']}B |\n")
        out.write("\n")

        for e in sorted(commands, key=lambda x: (x["component"], x["opcode"] or "")):
            opc = e["opcode"] or "?"
            out.write(f"### {e['component']} : {e['struct']} ({opc})\n\n")
            out.write(f"Payload size: {e['size']} bytes\n\n")
            emit_field_table(e["fields"], out)

    # 4. Telemetry
    out.write("## 4. Telemetry\n\n")
    if not telemetry:
        out.write("No component-specific telemetry defined.\n\n")
    else:
        out.write("### Telemetry Summary\n\n")
        out.write("| Component | Opcode | Struct | Size |\n")
        out.write("|-----------|--------|--------|------|\n")
        for e in sorted(telemetry, key=lambda x: (x["component"], x["opcode"] or "")):
            opc = e["opcode"] or "-"
            out.write(f"| {e['component']} | {opc} | " f"`{e['struct']}` | {e['size']}B |\n")
        out.write("\n")

        for e in sorted(telemetry, key=lambda x: (x["component"], x["opcode"] or "")):
            opc = e["opcode"] or "?"
            out.write(f"### {e['component']} : {e['struct']} ({opc})\n\n")
            out.write(f"Response size: {e['size']} bytes\n\n")
            emit_field_table(e["fields"], out)

    # 5. Tunable parameters
    out.write("## 5. Tunable Parameters\n\n")
    out.write(
        "Tunable parameters can be modified at runtime via TPRM files "
        "or APROTO SET_DATA commands.\n\n"
    )
    if not tunables:
        out.write("No tunable parameters defined.\n\n")
    else:
        out.write("### Tunable Summary\n\n")
        out.write("| Component | Struct | Size | Fields |\n")
        out.write("|-----------|--------|------|--------|\n")
        for e in sorted(tunables, key=lambda x: x["component"]):
            out.write(
                f"| {e['component']} | `{e['struct']}` | " f"{e['size']}B | {len(e['fields'])} |\n"
            )
        out.write("\n")

        for e in sorted(tunables, key=lambda x: x["component"]):
            out.write(f"### {e['component']} : {e['struct']}\n\n")
            out.write(f"Size: {e['size']} bytes\n\n")
            emit_field_table(e["fields"], out)

    # 6. State data
    out.write("## 6. State Data\n\n")
    out.write("State data is read-only from the C2 interface.\n\n")
    if not states:
        out.write("No state data defined.\n\n")
    else:
        out.write("### State Summary\n\n")
        out.write("| Component | Struct | Size | Fields |\n")
        out.write("|-----------|--------|------|--------|\n")
        for e in sorted(states, key=lambda x: x["component"]):
            out.write(
                f"| {e['component']} | `{e['struct']}` | " f"{e['size']}B | {len(e['fields'])} |\n"
            )
        out.write("\n")

        for e in sorted(states, key=lambda x: x["component"]):
            out.write(f"### {e['component']} : {e['struct']}\n\n")
            out.write(f"Size: {e['size']} bytes\n\n")
            emit_field_table(e["fields"], out)

    # 7. Wire protocol
    out.write("## 7. Wire Protocol\n\n")
    if protocol:
        for e in sorted(protocol, key=lambda x: x["struct"]):
            out.write(f"### {e['struct']}\n\n")
            out.write(f"Size: {e['size']} bytes\n\n")
            emit_field_table(e["fields"], out)
    else:
        out.write("No protocol definitions found.\n\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generate a consolidated cmd/tlm deck from struct dictionaries."
    )
    parser.add_argument(
        "--db",
        required=True,
        help="Path to apex_data_db directory containing JSON struct dictionaries",
    )
    parser.add_argument(
        "--output",
        "-o",
        default=None,
        help="Output file path (default: stdout)",
    )
    args = parser.parse_args()

    dicts = load_dicts(args.db)

    if args.output:
        with open(args.output, "w") as f:
            generate_deck(dicts, f)
        count = len(dicts)
        print(
            f"[c2-deck] Generated {args.output} ({count} components)",
            file=sys.stderr,
        )
    else:
        generate_deck(dicts, sys.stdout)


if __name__ == "__main__":
    main()
