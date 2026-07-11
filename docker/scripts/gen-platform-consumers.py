#!/usr/bin/env python3
"""Generate the platform-derived consumer sections from mk/platforms.mk.

The registry is the single source of truth for target platforms; three consumer
files carry mechanical per-platform sections derived from it:

  docker-compose.yml          the leaf dev-<platform> services
  docker-compose.ci-cache.yml the registry-cache entries
  .github/workflows/docker-images.yml  the image GRAPH table

Each section lives between BEGIN/END GENERATED markers and is rewritten by
`make regen-platforms`; `make check-platforms` (CI lint) fails on drift, so the
committed files can never disagree with the registry. Adding a platform is a
registry row + toolchain + Dockerfile + presets -- these files regenerate.

stdlib only (the dev image carries no PyYAML); emission is line-oriented.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
REGISTRY = ROOT / "mk/platforms.mk"

FILES = {
    "compose": ROOT / "docker-compose.yml",
    "cicache": ROOT / "docker-compose.ci-cache.yml",
    "graph": ROOT / ".github/workflows/docker-images.yml",
}

BEGIN = "# --- BEGIN GENERATED: platform consumers (make regen-platforms) ---"
END = "# --- END GENERATED: platform consumers ---"


def parse_registry():
    text = REGISTRY.read_text()
    m = re.search(r"^PLATFORMS\s*:=((?:.*\\\n)*.*)$", text, re.M)
    plats = m.group(1).replace("\\", " ").split()
    fields = {}
    for name, field, val in re.findall(r"^P_([a-z0-9]+)_([A-Z]+)\s*:=[ \t]*(.*)$", text, re.M):
        fields.setdefault(name, {})[field] = val.strip()
    return plats, fields


def leaf_rows(plats, fields):
    """Leaves = every platform except the hosted cpu/cuda rows."""
    for p in plats:
        if p in ("cpu", "cuda"):
            continue
        f = fields.get(p, {})
        role = f.get("ROLE", "skeleton")
        yield {
            "name": p,
            "service": f.get("SERVICE", f"dev-{p}"),
            "base": f.get("BASE", "base"),
            "role": role,
            "published": role != "skeleton",
            "privileged": f.get("PRIVILEGED", "1") != "0" and role in ("cross-ship", "cross-port"),
            "common": role in ("cross-ship", "cross-port"),
        }


def gen_compose(rows):
    out = []
    cuda_children = [r for r in rows if r["base"] == "cuda"]
    cross = [r for r in rows if r["common"] and r["base"] != "cuda"]
    embedded = [r for r in rows if not r["common"]]

    def service(r):
        anchor = "common" if r["common"] else "embedded"
        out.append(f"  {r['service']}:")
        out.append(f"    <<: *{anchor}")
        out.append(f"    image: apex.dev.{r['name']}")
        out.append("    build:")
        out.append("      context: .")
        out.append(f"      dockerfile: docker/dev/{r['name']}.Dockerfile")
        out.append("      args:")
        out.append("        <<: *build-args")
        if r["base"] == "cuda":
            out.append("        BASE: apex.dev.cuda")
        out.append(f"    hostname: apex-{r['service']}")
        if r["common"]:
            if r["privileged"]:
                out.append("    privileged: true")
            else:
                out.append("    # Unprivileged (registry PRIVILEGED=0): needs no devices or module")
                out.append("    # loads; the vcan/perf/SD-imaging shells keep privilege.")
        if r["base"] == "cuda":
            out.append("    environment:")
            out.append("      - NVIDIA_VISIBLE_DEVICES=")
            out.append("      - NVIDIA_DRIVER_CAPABILITIES=")
            out.append("      - NVIDIA_DISABLE_REQUIRE=true")
        if r["common"]:
            out.append("    command: bash")
        out.append("")

    for r in cuda_children:
        service(r)
    out.append("  # ==========================================================================")
    out.append("  # Interactive development shells - Additional Linux targets")
    out.append("  # ==========================================================================")
    for r in cross:
        service(r)
    out.append("  # ==========================================================================")
    out.append("  # Interactive development shells - Embedded/MCU")
    out.append("  # ==========================================================================")
    for r in embedded:
        service(r)
    return "\n".join(out).rstrip() + "\n"


def gen_cicache(rows):
    out = []

    def entry(svc, image):
        out.append(f"  {svc}:")
        out.append("    build:")
        out.append("      cache_from:")
        out.append(f"        - ${{REGISTRY}}/{image}:latest")
        if svc == "base":
            out.append("        # dev-base shares build-base's lower half; reuse those layers too.")
            out.append("        - ${REGISTRY}/apex.build-base:latest")
        out.append("      cache_to: *inline-cache")
        out.append("")

    entry("base", "apex.base")
    entry("build-base", "apex.build-base")
    entry("dev", "apex.dev.cpu")
    entry("dev-cuda", "apex.dev.cuda")
    entry("cuda-build", "apex.cuda-build")
    for r in rows:
        if not r["published"]:
            continue
        entry(r["service"], f"apex.dev.{r['name']}")
        if r["name"] == "jetson":
            entry("build-jetson", "apex.build.jetson")
    skels = " / ".join(r["name"] for r in rows if not r["published"])
    out.append(f"  # {skels} are dev-shell-only platforms that CI does not")
    out.append("  # build, so they carry no registry-cache overlay here.")
    return "\n".join(out).rstrip() + "\n"


def gen_graph(rows):
    ind = " " * 10
    out = [
        f"{ind}# image | compose-service | parent-image | tier | dockerfile",
        f"{ind}# apex.base and apex.build-base are two targets of base.Dockerfile, so",
        f"{ind}# a base.Dockerfile change rebuilds both (and cascades to their",
        f"{ind}# children). apex.cuda-build is the lean CUDA tier on build-base,",
        f"{ind}# sharing cuda.Dockerfile with apex.dev.cuda; apex.build.jetson is its",
        f"{ind}# jetson child. Leaf rows derive from mk/platforms.mk (ROLE != skeleton).",
        f'{ind}GRAPH="',
        f"{ind}apex.base|base||0|docker/base.Dockerfile",
        f"{ind}apex.build-base|build-base||0|docker/base.Dockerfile",
        f"{ind}apex.dev.cpu|dev|apex.base|1|docker/dev/cpu.Dockerfile",
        f"{ind}apex.dev.cuda|dev-cuda|apex.base|1|docker/dev/cuda.Dockerfile",
        f"{ind}apex.cuda-build|cuda-build|apex.build-base|1|docker/dev/cuda.Dockerfile",
    ]
    parent = {"cpu": "apex.dev.cpu", "cuda": "apex.dev.cuda", "base": "apex.base"}
    for r in rows:
        if not r["published"]:
            continue
        df = f"docker/dev/{r['name']}.Dockerfile"
        out.append(f"{ind}apex.dev.{r['name']}|{r['service']}|{parent[r['base']]}|2|{df}")
        if r["name"] == "jetson":
            out.append(f"{ind}apex.build.jetson|build-jetson|apex.cuda-build|2|{df}")
    out.append(f'{ind}"')
    skels = ", ".join(r["name"] for r in rows if not r["published"])
    out.append(f"{ind}# {skels} are intentionally absent: dev-shell-only")
    out.append(f"{ind}# platforms (ROLE skeleton) -- no shipping artifact uses them, so CI")
    out.append(f"{ind}# does not build or publish them. make docker-dev-<name> builds one")
    out.append(f"{ind}# locally when a developer needs the shell.")
    return "\n".join(out) + "\n"


def splice(path, body, indent=""):
    text = path.read_text()
    b, e = indent + BEGIN, indent + END
    if b not in text or e not in text:
        sys.exit(f"markers missing in {path}")
    pre = text.split(b)[0]
    post = text.split(e)[1]
    return f"{pre}{b}\n{body}{e}{post}", text


def main():
    check = "--check" in sys.argv
    plats, fields = parse_registry()
    rows = list(leaf_rows(plats, fields))
    sections = {
        "compose": (FILES["compose"], gen_compose(rows), "  "),
        "cicache": (FILES["cicache"], gen_cicache(rows), "  "),
        "graph": (FILES["graph"], gen_graph(rows), " " * 10),
    }
    drift = []
    for _key, (path, body, indent) in sections.items():
        new, old = splice(path, body, indent)
        if new != old:
            if check:
                drift.append(str(path))
            else:
                path.write_text(new)
                print(f"[regen-platforms] wrote {path}")
    if check:
        if drift:
            print("[check-platforms] DRIFT (run make regen-platforms):")
            for d in drift:
                print(f"  {d}")
            sys.exit(1)
        print("[check-platforms] consumers match mk/platforms.mk")


if __name__ == "__main__":
    main()
