# The apex proto profile

Spec-defined component structs can be authored as protobuf messages
instead of inline `[[fields]]` arrays: the manifest names a `.proto`
file and the tools resolve its messages to the same ordered field
lists every downstream surface consumes (generated headers, command
dispatch, dictionaries, TPRM payloads, Zenith panels). Every
spec-defined component also **emits** its interface as
`.auto/<Component>.proto` in this profile, so proto-speaking
integrators get a compilable interface regardless of how the
component was authored.

The profile is deliberately narrow. The vehicle wire is format A
(fixed memory image, zero-parse); proto here is a **spec and interop
format**, never the RT encoding. The tools parse the profile with an
in-repo subset parser -- no protoc required; violations fail
generation with field-named errors.

---

## Referencing a proto spec

```toml
component = "SpecActuator"
namespace = "appsim::spec"
proto_spec = "spec_actuator.proto"   # relative to this manifest

[structs]
SpecActuatorTunableParams = { category = "TUNABLE_PARAM" }
MoveRequest = { category = "COMMAND", opcode = "0x0210" }
```

- One definition source per component: a manifest with `proto_spec`
  must have **no** `[[fields]]` arrays (and vice versa). Both present
  is an error, never a merge.
- Every message in the file must match a registered struct name; a
  registered struct without a message simply has no spec-defined
  layout (same as a struct without a `[[fields]]` array).
- Commands, telemetry, constraints, and enums stay in the TOML
  manifest -- the proto carries **layouts only**.

## File shape

```proto
syntax = "proto3";

package appsim.spec;

import "apex/options.proto";

message SpecActuatorTunableParams {
  // Slew rate limit [units/s], range 0..50.
  float rateLimit = 1 [(apex.default) = "5.0"];
  // Commanded-position hold band.
  float holdBand = 2 [(apex.default) = "0.1"];
  repeated uint32 reserved = 3 [(apex.width) = 1, (apex.count) = 2];
}
```

- `syntax = "proto3";` is required.
- `package`, when present, must equal the manifest `namespace` with
  `::` replaced by `.`.
- The only permitted import is `apex/options.proto`.
- **Declaration order is layout order**, and field numbers must be
  `1..N` in declaration order -- renumbering or reordering is loud,
  never silent.
- A `//` comment line (or lines) directly above a field becomes the
  field's doc; other comments are ignored.

## Type mapping

| proto declaration   | field type/size    | notes                                                    |
| ------------------- | ------------------ | -------------------------------------------------------- |
| `float`             | float / 4          |                                                          |
| `double`            | float / 8          |                                                          |
| `bool`              | bool / 1           |                                                          |
| `uint32`            | uint / 4           | `(apex.width)` narrows to 1, 2                           |
| `int32`             | int / 4            | `(apex.width)` narrows to 1, 2                           |
| `uint64`            | uint / 8           | width option not allowed                                 |
| `int64`             | int / 8            | width option not allowed                                 |
| `repeated <scalar>` | array of the above | `(apex.count)` **required**                              |
| `string`            | string / capacity  | `(apex.capacity)` **required** -> `char[N]`, null-padded |
| `repeated string`   | string array       | `(apex.capacity)` + `(apex.count)` -> `char[N][C]`       |
| `bytes`             | uint / 1 array     | `(apex.count)` **required** -> fixed byte array          |

The rule is bounded-or-yell, the nanopb static-mode discipline: every
type whose standard form is variable-length has exactly one legal
profile spelling, and it carries an explicit bound. The unbounded
spelling fails ingest with an error that names the fix
("unbounded string -- bound it with (apex.capacity) = <bytes>").
String content follows the TPRM value rule: fits or packing fails,
null-padded to capacity -- capacity is static, content varies.

A message-typed field is a nested struct reference: the message must
be defined in the same file, arrays are `repeated` with an
`(apex.count)` bound, and embedding runs at most two levels deep
(generation enforces the depth). Everything genuinely unboundable --
enums, maps, `oneof`, services, non-apex options -- is outside the
profile and fails ingest with a named error.

## Options

- `(apex.width) = N` -- byte width (1|2|4) for `uint32`/`int32`
  fields; absent means the natural 4.
- `(apex.count) = N` -- fixed element count; required on `repeated`
  and `bytes`, forbidden on scalars.
- `(apex.capacity) = N` -- byte capacity of a string's fixed char
  buffer; required on `string`, forbidden elsewhere.
- `(apex.default) = "literal"` -- member default; the literal parses
  by shape (`true`/`false` -> bool, decimal point or exponent ->
  float, otherwise integer) and must match the field type. Not
  applicable to strings (author the value in the TPRM TOML).

## Fixpoint guarantee

Emission is canonical, and the two directions are pinned by tests:
`ingest(emit(spec))` reproduces the identical ordered fields **and
layout hash**, and `emit(ingest(proto))` re-emits byte-identically.
An authored file may differ in formatting from the canonical form;
its `.auto/<Component>.proto` emission is the normalized interface.

## See also

- [apex/options.proto](../../proto/apex/options.proto) -- extension
  definitions for standard toolchains.
- [apex_data_gen](apex_data_gen.md) -- the manifest surfaces the
  proto profile feeds.
