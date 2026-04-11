"""Validate generated Zenith target config against a live Apex target.

Connects to a running Apex application via APROTO, queries GET_REGISTRY
and GET_DATA_CATALOG, and diffs the results against the generated
app_manifest.json and struct dictionaries.

Usage:
  zenith-validate --generated build/zenith_targets/ApexOpsDemo --host 192.168.1.119
  zenith-validate --generated build/zenith_targets/ApexOpsDemo --host localhost --port 9000
"""

import argparse
import json
import sys
from pathlib import Path

# Ensure the ops package is importable when run standalone
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from apex_tools.ops.client import AprotoClient  # noqa: E402

PASS_COUNT = 0
WARN_COUNT = 0
ERR_COUNT = 0


def check(label: str, ok: bool, detail: str = "") -> bool:
    global PASS_COUNT, WARN_COUNT
    if ok:
        PASS_COUNT += 1
        print(f"  OK    {label}")
    else:
        WARN_COUNT += 1
        msg = f"  WARN  {label}"
        if detail:
            msg += f"  ({detail})"
        print(msg)
    return ok


def error(label: str, detail: str = ""):
    global ERR_COUNT
    ERR_COUNT += 1
    msg = f"  ERR   {label}"
    if detail:
        msg += f"  ({detail})"
    print(msg)


def main():
    parser = argparse.ArgumentParser(
        description="Validate Zenith target config against a live Apex target"
    )
    parser.add_argument("--generated", required=True, help="Path to generated target directory")
    parser.add_argument("--host", required=True, help="Target hostname or IP")
    parser.add_argument("--port", type=int, default=9000, help="Target port (default: 9000)")
    parser.add_argument("--timeout", type=float, default=5.0, help="Command timeout (default: 5.0)")
    args = parser.parse_args()

    gen_dir = Path(args.generated)
    if not gen_dir.exists():
        print(f"[validate] ERROR: Generated directory not found: {gen_dir}")
        sys.exit(1)

    # Load generated manifest
    manifest_path = gen_dir / "app_manifest.json"
    if not manifest_path.exists():
        print(f"[validate] ERROR: app_manifest.json not found in {gen_dir}")
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    gen_components = {int(c["fullUid"], 16): c for c in manifest.get("components", [])}

    print(f"\n[validate] Connecting to {args.host}:{args.port}...")
    print(f"[validate] Generated config: {gen_dir}")
    print(f"[validate] Application: {manifest.get('application', '?')}")
    print(f"{'=' * 72}")

    try:
        with AprotoClient(args.host, args.port, timeout=args.timeout) as c2:
            # Connectivity check
            r = c2.noop()
            if r["status"] != 0:
                error("Connectivity", r["status_name"])
                print(f"\n{'=' * 72}")
                print(f"  Results: {PASS_COUNT} ok, {WARN_COUNT} warnings, {ERR_COUNT} errors")
                sys.exit(1)
            print("  OK    Connected")

            # --- Registry validation ---
            print(f"\n{'- ' * 36}")
            print("  Registry Validation")
            print(f"{'- ' * 36}")

            r = c2.get_registry()
            if r["status"] != 0:
                error("GET_REGISTRY failed", r["status_name"])
            else:
                live_components = r.get("components", [])
                live_uids = {c["full_uid"]: c for c in live_components}

                check(
                    f"Component count: generated={len(gen_components)} live={len(live_uids)}",
                    len(gen_components) == len(live_uids),
                )

                # Check each generated component exists in live registry
                for uid, gen_comp in sorted(gen_components.items()):
                    name = gen_comp.get("name", "?")
                    if uid in live_uids:
                        live_comp = live_uids[uid]
                        live_name = live_comp.get("name", "?")
                        # Name match (fuzzy: generated may use display name)
                        name_ok = name == live_name or name in live_name or live_name in name
                        check(
                            f"0x{uid:06X} {name} present",
                            True,
                        )
                        if not name_ok:
                            check(
                                f"0x{uid:06X} name: generated='{name}' live='{live_name}'",
                                False,
                                "name mismatch",
                            )
                        # Type match
                        gen_type = gen_comp.get("type", "")
                        live_type = live_comp.get("type_name", "")
                        check(
                            f"0x{uid:06X} type: {gen_type}",
                            gen_type == live_type,
                            f"live={live_type}" if gen_type != live_type else "",
                        )
                    else:
                        check(
                            f"0x{uid:06X} {name} present",
                            False,
                            "not in live registry",
                        )

                # Check for components in live but not in generated
                for uid, live_comp in sorted(live_uids.items()):
                    if uid not in gen_components:
                        check(
                            f"0x{uid:06X} {live_comp.get('name', '?')} in generated config",
                            False,
                            "exists in live but not in generated",
                        )

            # --- Data catalog validation ---
            print(f"\n{'- ' * 36}")
            print("  Data Catalog Validation")
            print(f"{'- ' * 36}")

            r = c2.get_data_catalog()
            if r["status"] != 0:
                error("GET_DATA_CATALOG failed", r["status_name"])
            else:
                live_data = r.get("data_entries", [])
                print(f"  INFO  Live data entries: {len(live_data)}")

                # Load struct dicts for size comparison
                structs_dir = gen_dir / "structs"
                struct_dicts = {}
                if structs_dir.exists():
                    for f in structs_dir.glob("*.json"):
                        with open(f) as fh:
                            struct_dicts[f.stem] = json.load(fh)

                # For each live data entry, check if its size matches the struct dict
                for entry in live_data:
                    uid = entry["full_uid"]
                    cat = entry["category_name"]
                    size = entry["size"]
                    name = entry["name"]

                    # Find the component name from live registry
                    comp_name = live_uids.get(uid, {}).get("name", f"0x{uid:06X}")

                    # Find matching struct in struct dicts
                    dict_data = struct_dicts.get(comp_name)
                    if dict_data:
                        # Search structs for matching category
                        matched = False
                        for _sname, sinfo in dict_data.get("structs", {}).items():
                            dict_cat = sinfo.get("category", "")
                            dict_size = sinfo.get("size", 0)
                            # Category mapping: TUNABLE_PARAM in registry vs struct dict
                            if _category_matches(cat, dict_cat):
                                matched = True
                                check(
                                    f"0x{uid:06X} {comp_name}.{name} ({cat}, {size}B)",
                                    size == dict_size,
                                    f"struct dict says {dict_size}B" if size != dict_size else "",
                                )
                                break
                        if not matched:
                            # Data exists at runtime but no matching struct in dict
                            check(
                                f"0x{uid:06X} {comp_name}.{name} ({cat}, {size}B) in struct dict",
                                False,
                                "no matching struct category in dict",
                            )
                    else:
                        # No struct dict for this component at all
                        check(
                            f"0x{uid:06X} {comp_name}.{name} ({cat}, {size}B) struct dict exists",
                            False,
                            f"no struct dict for {comp_name}",
                        )

    except OSError as e:
        error("Connection failed", str(e))

    # Summary
    print(f"\n{'=' * 72}")
    total = PASS_COUNT + WARN_COUNT + ERR_COUNT
    print(f"  Results: {PASS_COUNT} ok, {WARN_COUNT} warnings, {ERR_COUNT} errors (of {total})")
    print(f"{'=' * 72}")

    sys.exit(1 if ERR_COUNT > 0 else 0)


def _category_matches(live_cat: str, dict_cat: str) -> bool:
    """Check if a live data category matches a struct dict category."""
    # Direct match
    if live_cat == dict_cat:
        return True
    # Common mappings
    mapping = {
        "TUNABLE_PARAM": ("TUNABLE_PARAM",),
        "STATE": ("STATE",),
        "OUTPUT": ("OUTPUT", "TELEMETRY"),
        "INPUT": ("INPUT",),
        "STATIC_PARAM": ("STATIC_PARAM",),
    }
    return dict_cat in mapping.get(live_cat, ())


if __name__ == "__main__":
    main()
