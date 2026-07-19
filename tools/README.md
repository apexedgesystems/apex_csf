## Adding a tool family

The tree is built so a new language family (say, Go) slots in with these
steps -- every one of them, honestly:

1. **Sources**: `tools/go/` with its native manifest (`go.mod`).
2. **Build section**: add a gated block in `tools/CMakeLists.txt` mirroring
   the rust one (find the toolchain, generated build script into
   `bin/tools/go`, `add_custom_target(${PROJECT_NAME}_go_tools)`, append to
   `_TOOLS_DEPS`) and add `go` to `_APEX_TOOL_FAMILIES` at the top -- the
   env helper picks up the PATH entry from the list.
3. **Make surface**: add `go` to `TOOL_FAMILIES` in `mk/tools.mk`
   (`tools-go` and the `tools` aggregate follow), and give the family an
   executed check (`test-go` in `mk/test.mk`, plus a CI classifier scope
   in `.github/workflows/ci.yml` if it should gate independently).
4. **Hermetic bake**: install the toolchain and bake an offline module
   cache in `docker/base.Dockerfile` (the cargo/uv sections are the
   patterns), or release builds will fail offline.
5. **Release assets**: executables under `bin/tools/go` ride the
   apex-tools tarball automatically (`docker/scripts/package-tools.sh`
   packages the whole tree). Only a language-native package artifact
   (like the python wheel) needs a packaging change -- extend
   package-tools.sh, never a second implementation.
