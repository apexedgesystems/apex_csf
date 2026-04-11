#!/bin/bash
# ==============================================================================
# ops_sdk_package.sh - Operations integration SDK packager
#
# Bundles struct dictionaries, runtime metadata exports, and a quick-start
# README into a self-contained package for operations integrators. The package
# contains everything needed to build a ground station or control interface
# without access to the Apex source code or build system.
#
# Usage:
#   ops_sdk_package.sh --app <name> --build-dir <path> [options]
#
# Options:
#   --app <name>          Application name (required)
#   --build-dir <path>    Build directory (required)
#   --output <path>       Output directory (default: <build-dir>/ops_sdk)
#   --port <port>         Default TCP port to document (default: 9000)
#   --help                Show this help
#
# Output:
#   <output>/<app>-ops-sdk.tar.gz
#     <app>/structs/*.json          Struct dictionaries
#     <app>/runtime/registry.rdat   Component metadata (if available)
#     <app>/runtime/scheduler.sdat  Task schedule (if available)
#     <app>/README.md               Quick-start guide
#
# ==============================================================================

set -euo pipefail

readonly SCRIPT_NAME="ops-sdk"

log() { printf '\033[36m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }
log_ok() { printf '\033[32m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }
err() { printf '\033[31m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$1" >&2; }

# ==============================================================================
# CLI Parsing
# ==============================================================================

APP=""
BUILD_DIR=""
OUTPUT_DIR=""
PORT=9000

usage() {
  sed -n '2,/^# ==/{ /^#/s/^# \{0,1\}//p }' "$0"
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
  --app)
    APP="$2"
    shift 2
    ;;
  --build-dir)
    BUILD_DIR="$2"
    shift 2
    ;;
  --output)
    OUTPUT_DIR="$2"
    shift 2
    ;;
  --port)
    PORT="$2"
    shift 2
    ;;
  --help | -h) usage ;;
  *)
    err "Unknown option: $1"
    exit 1
    ;;
  esac
done

[[ -z "$APP" ]] && {
  err "--app is required"
  exit 1
}
[[ -z "$BUILD_DIR" ]] && {
  err "--build-dir is required"
  exit 1
}
[[ -d "$BUILD_DIR" ]] || {
  err "Build directory not found: $BUILD_DIR"
  exit 1
}

OUTPUT_DIR="${OUTPUT_DIR:-$BUILD_DIR/ops_sdk}"

# ==============================================================================
# Stage SDK Contents
# ==============================================================================

SDK_DIR="$OUTPUT_DIR/$APP"
rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR/structs" "$SDK_DIR/runtime"

# Copy struct dictionaries
JSON_COUNT=0
if [[ -d "$BUILD_DIR/apex_data_db" ]]; then
  for json in "$BUILD_DIR/apex_data_db"/*.json; do
    [[ -f "$json" ]] || continue
    cp -f "$json" "$SDK_DIR/structs/"
    JSON_COUNT=$((JSON_COUNT + 1))
  done
fi

if [[ "$JSON_COUNT" -eq 0 ]]; then
  err "No struct dictionaries found in $BUILD_DIR/apex_data_db/"
  err "Run 'make apex-data-db' first"
  exit 1
fi

log "Included $JSON_COUNT struct dictionaries"

# Copy runtime exports (optional -- only available after a test run)
APEX_FS="$BUILD_DIR/bin/.apex_fs"

if [[ -f "$APEX_FS/db/registry.rdat" ]]; then
  cp -f "$APEX_FS/db/registry.rdat" "$SDK_DIR/runtime/"
  log "Included runtime registry.rdat"
else
  log "No registry.rdat found (run the app first to generate)"
fi

if [[ -f "$APEX_FS/db/scheduler.sdat" ]]; then
  cp -f "$APEX_FS/db/scheduler.sdat" "$SDK_DIR/runtime/"
  log "Included runtime scheduler.sdat"
fi

# ==============================================================================
# Generate README
# ==============================================================================

cat >"$SDK_DIR/README.md" <<READMEEOF
# $APP Operations Integration SDK

## Contents

- \`structs/*.json\` -- Struct dictionaries describing every data block
  registered by the application. Each JSON file contains field names, types,
  byte offsets, and sizes for all STATE, TUNABLE_PARAM, OUTPUT, and INPUT
  data categories.

- \`runtime/registry.rdat\` -- Binary metadata exported at runtime listing
  all component instances, their fullUids, tasks, and data blocks. Use
  \`rdat_tool --input runtime/registry.rdat --list\` to inspect.

- \`runtime/scheduler.sdat\` -- Binary metadata exported at runtime listing
  the task schedule. Use \`sdat_tool --input runtime/scheduler.sdat --list\`
  to inspect.

## Connecting

The $APP application listens on TCP port $PORT (configurable via TPRM).
All traffic uses the APROTO binary protocol with SLIP framing.

\`\`\`
[SLIP_END] [APROTO Header 14B] [Payload 0-64KB] [CRC32 4B opt] [SLIP_END]
\`\`\`

APROTO header (14 bytes, little-endian):

| Offset | Size | Field         | Description                                |
|--------|------|---------------|--------------------------------------------|
| 0      | 2    | magic         | 0x5041 ("AP")                              |
| 2      | 1    | version       | 1                                          |
| 3      | 1    | flags         | bit0=internal, bit1=response,              |
|        |      |               | bit2=ackReq, bit3=crc, bit4=encrypt        |
| 4      | 4    | fullUid       | Target component                           |
| 8      | 2    | opcode        | Operation (0x0001=PING, 0x0002=GET_STATUS) |
| 10     | 2    | sequence      | Correlator for ACK/NAK                     |
| 12     | 2    | payloadLength | Payload size                               |

## Addressing

Components are addressed by \`fullUid = (componentId << 8) | instanceIndex\`.
See \`runtime/registry.rdat\` for the full list of registered components.

## System Opcodes

| Opcode | Name       | Description        |
|--------|------------|--------------------|
| 0x0000 | NOOP       | No-op, returns ACK |
| 0x0001 | PING       | Echo payload       |
| 0x0002 | GET_STATUS | Component status   |
| 0x0003 | RESET      | Reset component    |
| 0x00FE | ACK        | Acknowledgment     |
| 0x00FF | NAK        | Negative ack       |

Component-specific opcodes start at 0x0100.

## SLIP Framing

SLIP is a simple byte-stuffing protocol (RFC 1055):
- Frame delimiter: 0xC0 (END)
- Escape: 0xDB (ESC), followed by 0xDC (ESC_END) or 0xDD (ESC_ESC)
- Send END before and after each packet

## Quick Test (Python)

\`\`\`python
import socket, struct

SLIP_END = 0xC0
AP_MAGIC = 0x5041

def slip_frame(data):
    """SLIP-encode a raw byte sequence."""
    out = bytes([SLIP_END])
    for b in data:
        if b == SLIP_END: out += bytes([0xDB, 0xDC])
        elif b == 0xDB:   out += bytes([0xDB, 0xDD])
        else:             out += bytes([b])
    return out + bytes([SLIP_END])

def slip_decode(data):
    """SLIP-decode a framed byte sequence."""
    out, i = bytearray(), 0
    while i < len(data):
        if data[i] == SLIP_END:
            i += 1; continue
        if data[i] == 0xDB:
            i += 1
            if data[i] == 0xDC: out.append(SLIP_END)
            elif data[i] == 0xDD: out.append(0xDB)
        else:
            out.append(data[i])
        i += 1
    return bytes(out)

def build_packet(full_uid, opcode, sequence=1, payload=b'', flags=0):
    """Build a raw APROTO packet (no CRC)."""
    hdr = struct.pack('<HBBIHH', AP_MAGIC, 1, flags, full_uid, opcode, sequence)
    hdr += struct.pack('<H', len(payload))
    return hdr + payload

# Connect
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('TARGET_HOST', $PORT))

# Send PING to executive (fullUid=0)
pkt = build_packet(0x000000, 0x0001, sequence=1, payload=b'hello')
sock.send(slip_frame(pkt))

# Read response
resp = sock.recv(4096)
decoded = slip_decode(resp)
print(f'Response ({len(decoded)} bytes): {decoded.hex()}')

sock.close()
\`\`\`

Replace \`TARGET_HOST\` with the IP or hostname of the target system.

## Reading Component Data

To read a component's STATE data, use the struct dictionary to know the
byte layout, then request via APROTO:

\`\`\`python
import json

# Load struct dictionary
with open('structs/HilPlantModel.json') as f:
    schema = json.load(f)

# Find the STATE struct
for name, info in schema['structs'].items():
    if info.get('category') == 'STATE':
        print(f'Struct: {name}, Size: {info["size"]} bytes')
        for field in info['fields']:
            print(f'  {field["name"]}: {field["type"]} '
                  f'(offset={field["offset"]}, size={field["size"]})')
\`\`\`

## Writing Tunable Parameters

TUNABLE_PARAM data blocks can be written at runtime to adjust component
behavior without restarting. Use the struct dictionary to construct the
binary payload, then send via APROTO with the appropriate opcode.

## Encryption

When \`encryptedPresent=1\` (flags bit 4), a 13-byte CryptoMeta block
follows the header:

| Offset | Size | Field    | Description                    |
|--------|------|----------|--------------------------------|
| 0      | 1    | keyIndex | Pre-shared key table index     |
| 1      | 12   | nonce    | AEAD nonce (unique per message) |

The payload is AEAD ciphertext with a 16-byte auth tag appended.
The header (14 bytes) is used as AAD.
READMEEOF

log "Generated README.md"

# ==============================================================================
# Create Tarball
# ==============================================================================

tar -czf "$OUTPUT_DIR/$APP-ops-sdk.tar.gz" -C "$OUTPUT_DIR" "$APP"
TARBALL_SIZE=$(du -sh "$OUTPUT_DIR/$APP-ops-sdk.tar.gz" | cut -f1)
log_ok "Package: $OUTPUT_DIR/$APP-ops-sdk.tar.gz ($TARBALL_SIZE, $JSON_COUNT struct files)"
