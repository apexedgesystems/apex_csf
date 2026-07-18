#!/usr/bin/env bash
# ==============================================================================
# bake-external-deps.sh - pre-stage the apex build's external dependencies into
# the dev image for hermetic, offline release builds.
#
# Reads the FetchContent declarations in ExternalDependencies.cmake (the single
# source of truth for the pinned versions) and clones each dependency at its
# pinned GIT_TAG into <dest>, so CMake FetchContent uses the local copy via
# FETCHCONTENT_SOURCE_DIR_<NAME> -- no GitHub at configure time.
#
# A dependency that ships its own tools is baked too: its cargo crates are
# fetched into the shared CARGO_HOME (the dependency's build inherits it -- e.g.
# vernier >= 1.0.3) and its locked python wheels are downloaded into the
# wheelhouse <wheel-dest>, so the dependency's tools also build offline inside
# the apex build.
#
# Usage: bake-external-deps.sh <ExternalDependencies.cmake> <dest-dir> [wheel-dest]
# ==============================================================================
set -euo pipefail

CMAKE_FILE="${1:?usage: bake-external-deps.sh <ExternalDependencies.cmake> <dest-dir> [wheel-dest]}"
DEST="${2:?usage: bake-external-deps.sh <ExternalDependencies.cmake> <dest-dir> [wheel-dest]}"
WHEEL_DEST="${3:-}"
mkdir -p "$DEST"

# Extract (name repo tag) for every fetchcontent_declare() block. The name is
# the first token after the opening line; GIT_REPOSITORY/GIT_TAG follow.
parse_decls() {
  awk '
    # Strip comments first so a "#" line (or an inline "# ... )" with parens)
    # cannot be mistaken for the closing paren of a declare block.
    { sub(/#.*/, "") }
    tolower($0) ~ /fetchcontent_declare\(/ { inblk = 1; name = ""; repo = ""; tag = "" ; next }
    inblk && name == "" && $1 != "" && $1 != "SYSTEM" { name = $1 }
    inblk && $1 == "GIT_REPOSITORY" { repo = $2 }
    inblk && $1 == "GIT_TAG" { tag = $2 }
    inblk && $0 ~ /\)/ {
      if (name != "" && repo != "" && tag != "") print name, repo, tag
      inblk = 0
    }
  ' "$CMAKE_FILE"
}

while read -r name repo tag; do
  [ -n "$name" ] || continue
  echo "[bake] ${name} @ ${tag} <- ${repo}"
  # A pin is an immutable tag or a full commit SHA. `git clone --branch` only
  # takes refs, so fetch SHA pins explicitly.
  if [[ "$tag" =~ ^[0-9a-fA-F]{40}$ ]]; then
    git init --quiet "$DEST/$name"
    git -C "$DEST/$name" remote add origin "$repo"
    git -C "$DEST/$name" fetch --quiet --depth 1 origin "$tag"
    git -C "$DEST/$name" checkout --quiet FETCH_HEAD
  else
    git clone --quiet --branch "$tag" --depth 1 "$repo" "$DEST/$name"
  fi
  rm -rf "$DEST/$name/.git"

  # Dependency rust tools: fetch the locked crates into the shared CARGO_HOME.
  # --target keeps the cache to the host triple (no Windows/wasm crates).
  if [ -f "$DEST/$name/tools/rust/Cargo.lock" ]; then
    echo "[bake]   ${name}: cargo fetch (tools/rust)"
    (cd "$DEST/$name/tools/rust" &&
      cargo fetch --locked --target x86_64-unknown-linux-gnu)
  fi

  # Dependency python tools: download the locked wheels into the wheelhouse.
  # uv.lock exports natively; a dependency still locking with poetry gets its
  # lock parsed directly (the TOML carries name/version), so the image needs
  # no poetry. Runtime groups only -- a dependency's build inside the apex
  # image never runs its dev tooling, and all-groups was measured at +432MB
  # of image for zero consumers.
  if [ -n "$WHEEL_DEST" ] && [ -f "$DEST/$name/tools/py/uv.lock" ]; then
    echo "[bake]   ${name}: pip download (tools/py, uv.lock)"
    (cd "$DEST/$name/tools/py" &&
      uv export --frozen --format requirements-txt --no-dev --no-emit-project \
        --no-hashes --output-file /tmp/bake-req.txt &&
      pip3 download --no-cache-dir --requirement /tmp/bake-req.txt --dest "$WHEEL_DEST" &&
      rm -f /tmp/bake-req.txt)
  elif [ -n "$WHEEL_DEST" ] && [ -f "$DEST/$name/tools/py/poetry.lock" ]; then
    echo "[bake]   ${name}: pip download (tools/py, poetry.lock)"
    python3 - "$DEST/$name/tools/py/poetry.lock" >/tmp/bake-req.txt <<'PYEOF'
import sys
import tomllib

with open(sys.argv[1], "rb") as f:
    lock = tomllib.load(f)
for pkg in lock.get("package", []):
    # Older locks carry no groups field; treat those as runtime.
    if "main" in pkg.get("groups", ["main"]):
        print(pkg["name"] + "==" + pkg["version"])
PYEOF
    # One download per pin, no resolver: the lock lists packages for every
    # environment (duplicate names under different markers), which a joint
    # pip resolution rejects. A wheelhouse needs the files, not a solve.
    while IFS= read -r req; do
      pip3 download --no-cache-dir --no-deps "$req" --dest "$WHEEL_DEST"
    done </tmp/bake-req.txt
    rm -f /tmp/bake-req.txt
  fi
done < <(parse_decls)

echo "[bake] done -> $DEST"
ls -1 "$DEST"
