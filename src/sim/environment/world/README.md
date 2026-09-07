# sim_environment_world

World bundle container: one packed, hash-identified binary per
simulated world (`<body>.world.tprm`) collecting the content artifacts
that construct it — gravity coefficient table (`.grav`), terrain
tiling (`.htile`), atmosphere table (`.atm`) — as role-keyed entries.

The bundle is the content sibling of the component tprm family: the
same identity discipline (magic, version, uid, hash), in a container
sized for bulk. The v3 component payload carries a 16-bit size and
whole-body reads; bundle entries carry 64-bit offsets and are read in
place, so boot pays only for the entries a fidelity actually loads.

## Identity layers

- **bundleContentHash** (FNV-1a 64) — covers every byte after the
  64-byte header (entry table + payloads; the hash field self-excludes
  by construction). This is the value a consumer tprm pins; the master
  thereby authorizes world content transitively.
- **Per-entry crc32** (CRC-32/ISO-HDLC, the tprm family function) —
  payload bytes only, verified on every read.
- **Per-entry specHash** — the inner artifact's own provenance hash,
  copied verbatim at pack time; the artifact stays the authority on
  its identity (zero when the format predates headers).

## Uid range

Bundle uids live in the reserved world componentId range
`[0x0100, 0x01FF]` — above the single-byte space runtime components
allocate from, so collision is structural, not conventional. The
instance byte stays zero.

## File suffix

Bundles ship as `.world.tprm` (WORLD_FILE_SUFFIX): the inner-format
suffixes identify entry payloads inside the container.
