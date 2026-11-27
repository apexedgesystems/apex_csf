# tools/ops

What operating a deployed apex system uses: parameter templates, the
command and telemetry deck, ground-system target configuration, the
operations SDK, and checks of the hardware a deployment talks to. Nothing
in the apex build depends on this role.

## Tools

Executables install to `bin/tools/ops/` in the build directory.

| Tool                 | What it does                                                               | Invoked by            | Page                                                                 |
| -------------------- | -------------------------------------------------------------------------- | --------------------- | -------------------------------------------------------------------- |
| `tprm_template`      | Generates TOML parameter templates from C++ headers or struct dictionaries | `make tprm-templates` | [tprm_template](rust/apex-ops-tools/docs/tprm_template.md)           |
| `upx_tool`           | Tests, compresses, verifies and compares UPX-packed binaries               | by hand               | [upx_tool](rust/apex-ops-tools/docs/upx_tool.md)                     |
| `serial_dev_checker` | Scans serial devices and reports their status                              | by hand               | [serial_dev_checker](rust/apex-ops-tools/docs/serial_dev_checker.md) |
| `serial_dev_tester`  | Verifies serial loopback and interconnect wiring                           | by hand               | [serial_dev_tester](rust/apex-ops-tools/docs/serial_dev_tester.md)   |
| `ops-deck`           | Generates the consolidated command and telemetry deck                      | `make ops-deck`       | [ops-deck](py/apex_ops/docs/ops_deck.md)                             |
| `zenith-target`      | Generates the Zenith target configuration for one application              | `make zenith-target`  | [zenith-target](py/apex_ops/docs/zenith_target.md)                   |
| `ops_sdk_package.sh` | Packages the operations integration SDK for one application                | `make ops-sdk`        | [ops_sdk_package](sh/docs/ops_sdk_package.md)                        |

The Python package `apex_ops` also holds the APROTO client and protocol
modules the two Python tools share, and the validator behind
`make zenith-validate`, which checks a generated target against a running
one.

How each language's units build:
[../docs/rust_workspace.md](../docs/rust_workspace.md),
[../docs/python_tools.md](../docs/python_tools.md), [sh/](sh/README.md).

## Build and test

```bash
make tools-ops                   # the role's executables and wheel
make test-rust ROLES=ops         # its Rust tests
make clippy ROLES=ops            # its lints
make test-py                     # the Python suite, which holds the target-generation tests
make test-sh                     # the shell smoke (the SDK package's shape)
```

The products built from the struct dictionaries, each of which builds the
dictionaries first:

```bash
make tprm-templates              # TOML templates, one per tunable-parameter struct
make ops-deck                    # the command and telemetry deck
make ops-artifacts               # dictionaries, templates and deck together
make ops-sdk APP=<app>           # the operations SDK
make zenith-target APP=<app>     # the Zenith target configuration
```
