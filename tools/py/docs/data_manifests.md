# data_manifests

Inventory of the component data manifests the dictionary generator reads,
checked against what CMake registers. `make check-data-db` runs it first and
generates every manifest it lists.

## Rules

- A candidate is any `*_data.toml` or `apex_data.toml` under the roots,
  outside `build/` and hidden directories.
- A candidate whose TOML carries a top-level `component` key is a component
  data manifest. One carrying `application` is an application descriptor
  and is skipped. Neither, or both, is an error. Membership comes from the
  parsed document, never from the spelling of the key.
- Registration is every `apex_add_data_manifest(...)` call in the
  CMakeLists.txt files under the same roots: the default `apex_data.toml`
  beside the CMakeLists, or the `MANIFEST` path with
  `${CMAKE_CURRENT_SOURCE_DIR}` resolved and `..` collapsed. Comments do not
  count.
- The discovered set and the registered set must be equal, and every
  registered file must exist. An empty inventory is an error.

## Usage

```bash
# One manifest path per line, exit 1 with a message on any problem
python3 tools/py/src/apex_tools/ops/data_manifests.py --roots demos src apps

# Discovery only, without the registration comparison
python3 tools/py/src/apex_tools/ops/data_manifests.py --roots demos src apps \
  --no-registration-check
```

## Exit status

| Code | Meaning                                                              |
| ---- | -------------------------------------------------------------------- |
| 0    | Inventory printed                                                    |
| 1    | Unparseable or ambiguous file, registration mismatch, or no manifest |
