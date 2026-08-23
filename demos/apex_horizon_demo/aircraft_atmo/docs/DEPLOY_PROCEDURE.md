# ApexAircraftAtmoDemo Deploy Procedure

Build, verify, package, and pair the producer with a host-side
visualization consumer implementing the ACFT/1 wire format. The
producer runs in the dev container (or as a packaged release on the
same machine), sharing the host IPC namespace.

## Prerequisites

- Docker Compose environment configured (the dev services set
  `ipc: host`).
- The generated USSA76 atmosphere table in `data/` (see
  [HOW_TO_RUN.md](HOW_TO_RUN.md) — producer-side world tooling,
  bit-deterministic, identity by header spec_hash).
- A consumer implementing ACFT/1 (optional — the demo verifies
  headless; the ring back-pressures and command ingress stays live).

## 1. Build + verify

```bash
docker compose run --rm cuda-build make debug     # binary + generated master.tprm
docker compose run --rm cuda-build make testp     # full suite
```

The aircraft-specific gates ride the normal suites: wire-layout pins,
command validation, excitation windows, capture + boot-to-trim
goldens, and the aerodynamics mode validations.

## 2. Run

Launch through the `cuda-rt` compose service — it carries the rtprio
rlimit, so the executive's FIFO thread table applies (clock 90 /
tasks 80). Run flow, detached variant, and the boot-identity
verification greps (compiled dt, atmosphere spec_hash, thread
scheduling classes, heartbeat) are in
[HOW_TO_RUN.md](HOW_TO_RUN.md) — verify the identity before pairing.

## 3. Package + run as a deployment

The app declares a single-executive apex deployment
(`apex_add_deployment`, `master.tprm` bundled), so the standard
packaging flow applies:

```bash
# Stage: cmake --install components -> bank_a/{bin,libs,tprm} + run.sh + tarball
docker compose run --rm dev-cuda \
  cmake --build build/hosted-x86_64-debug --target package_ApexAircraftAtmoDemo

# Run the package -- zero arguments; run.sh resolves the executive,
# makes the deployment dir the filesystem root, and wires --config to
# the bundled TPRM
./build/hosted-x86_64-debug/packages/ApexAircraftAtmoDemo/run.sh
```

No packed binaries or data files are committed — the master generates
from the manifest at build time and the atmosphere table regenerates
from its tracked spec. Install it into the package before first run,
mirroring the repo-relative path the TPRM carries (the packaged
launcher runs with the deployment directory as cwd):

```bash
PKG=build/hosted-x86_64-debug/packages/ApexAircraftAtmoDemo
mkdir -p $PKG/demos/apex_horizon_demo/aircraft_atmo/data
cp demos/apex_horizon_demo/aircraft_atmo/data/earth_ussa76.atm \
  $PKG/demos/apex_horizon_demo/aircraft_atmo/data/
```

Skipping this is caught at boot: the executive's atmosphere smoke
check fails fast and the `spec_hash` identity grep comes up empty.
The launcher passes `--skip-cleanup`, so shutdown archives nothing
and the deployment stays intact across runs.

## 4. Pair

Boot order is irrelevant (the consumer attach-retries). Verification
at attach, from logs alone:

- Both processes print the same atmosphere identity
  (`spec_hash=...`) at load.
- The bridge init line shows ACFT v1 / capacity 16 / payload 256/256
  on `/horizon_aircraft`.
- The 1 Hz bridge health line carries publish/inactive/command
  counters; consumer departures report as `consumer inactive` (INFO,
  routine for ephemeral sessions).

Commands (pass-1 surface): SET_TURBULENCE_ENABLE 0x0100 and
SET_GUST_ALLEVIATION_ENABLE 0x0101, 1-byte payloads, APROTO frames on
the reverse ring addressed to 0x00E000 — a host-side recipe lives in
HOW_TO_RUN.md.
