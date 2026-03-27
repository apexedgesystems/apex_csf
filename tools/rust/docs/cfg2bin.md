# cfg2bin

Convert TOML or JSON configuration files to raw binary blobs for TPRM tunable parameters.

---

## Overview

`cfg2bin` reads a typed TOML or JSON configuration file and serializes each
field to a raw binary blob. The output byte layout matches the memory layout
of the corresponding C++ struct, making it suitable for direct loading via
`hex2cpp()` or distribution as a `.tprm` file.

---

## Options

```
cfg2bin --config <file> [--output <file>]
```

| Option                | Description                                             |
| --------------------- | ------------------------------------------------------- |
| `-c, --config <file>` | Input TOML or JSON configuration file (required).       |
| `-o, --output <file>` | Output path. Defaults to `<input-stem>.bin` if omitted. |

---

## Input Format

Each field entry specifies `type`, `size`, and `value`. Fields are serialized
in declaration order. Metadata keys starting with `__` (e.g. `__note__`) are
skipped. All multi-byte values are little-endian.

### Supported types

| Type     | Sizes      | Notes                                 |
| -------- | ---------- | ------------------------------------- |
| `uint`   | 1, 2, 4, 8 | Unsigned integer, little-endian.      |
| `int`    | 1, 2, 4, 8 | Signed integer, little-endian.        |
| `float`  | 4, 8       | IEEE 754 float/double, little-endian. |
| `bool`   | 1          | Serialized as 0 or 1.                 |
| `char`   | 1          | Single ASCII character.               |
| `string` | N          | Fixed-size, null-padded.              |
| `array`  | N          | Requires `element_type`.              |
| `struct` | N          | Requires nested `fields`.             |

### Example TOML input

```toml
[MyTunableParams.gainP]
type = "float"
size = 4
value = 2.0

[MyTunableParams.gainD]
type = "float"
size = 4
value = 1.5

[MyTunableParams.maxThrust]
type = "float"
size = 4
value = 200.0
```

---

## Exit Codes

| Code | Meaning                       |
| ---- | ----------------------------- |
| 0    | Success.                      |
| 1    | Parse or serialization error. |

---

## Examples

```bash
# Convert TOML config to binary (output: my_params.bin)
cfg2bin --config configs/my_params.toml

# Explicit output path
cfg2bin --config configs/my_params.toml --output build/my_params.tprm
```

### Loading in C++

```cpp
#include "src/system/core/components/filesystem/inc/Files.hpp"

MyTunableParams params;
std::string error;
if (!apex::filesystem::hex2cpp("tprm/my_params.tprm", params, error)) {
    // handle error
}
```

---

## See Also

- [tprm_template](tprm_template.md) -- Generates the TOML template that `cfg2bin` consumes.
- [tprm_pack](tprm_pack.md) -- Packs individual `.tprm` files into a master archive.
