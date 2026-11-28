# cdef_gen

Generates a component's committed `.auto` headers from the specs in its
`apex_data.toml`.

---

## Overview

Reads one component manifest and writes one `<Struct>_auto.hpp` for every
struct the manifest defines by spec: an ordered `[[fields.<Struct>]]` array
in the manifest, or a `proto_spec` file it names. Each header holds the
struct, a `static_assert` on its size and on every field's offset, and the
layout-hash constant the dictionary and the wire prelude are checked
against. A manifest that declares commands or tasks also gets
`<Component>SpecBase_auto.hpp`, and every spec-defined component gets
`<Component>.proto`, its interface written in the proto profile.

The generated files are committed. `make check-cdef` regenerates them into
a scratch directory and fails on any difference, so a hand-edited generated
file cannot merge. A manifest with no field spec and no command
declaration generates nothing and exits 0: adoption is per struct.

---

## Options

```
cdef_gen [options] --manifest <path>
```

| Option               | Description                                                                                                                                  |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `--manifest <path>`  | The component's `apex_data.toml` (required).                                                                                                 |
| `-o, --output <dir>` | Where the headers are written (optional; default `<manifest dir>/.auto`).                                                                    |
| `--stub`             | Also write the once-only component stub into `<manifest dir>/inc` (optional). Refuses to overwrite: the stub is the user's after generation. |
| `-h, --help`         | Print help.                                                                                                                                  |

---

## Examples

Regenerate one component's headers in place:

```bash
cdef_gen --manifest demos/apex_spec_demo/limits/apex_data.toml
```

It prints each file written and a count:

```
demos/apex_spec_demo/limits/.auto/NudgeRequest_auto.hpp
demos/apex_spec_demo/limits/.auto/SpecLimitsState_auto.hpp
demos/apex_spec_demo/limits/.auto/SpecLimitsTunableParams_auto.hpp
demos/apex_spec_demo/limits/.auto/SpecLimitsSpecBase_auto.hpp
demos/apex_spec_demo/limits/.auto/SpecLimits.proto
5 header(s) generated
```

Generate into a scratch directory to compare with the committed tree, which
is what the gate does:

```bash
cdef_gen --manifest demos/apex_spec_demo/limits/apex_data.toml --output /tmp/auto
diff -ru demos/apex_spec_demo/limits/.auto /tmp/auto
```

Through the build, for every manifest that carries a spec:

```bash
make cdef          # regenerate all committed .auto headers
make check-cdef    # fail if any differs from its spec
```

---

## See Also

- [apex_data_gen](apex_data_gen.md) -- the manifest's field definitions,
  and the dictionaries generated from the same specs
- [proto profile](../../apex-tprm/docs/proto_profile.md) -- specs authored
  as `.proto` files
