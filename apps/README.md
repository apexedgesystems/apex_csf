# apps/ -- User Application Space

This tree is for **your** production applications. Everything apex ships as
an example lives under [demos/](../demos/); nothing in `apps/` is touched by
apex upgrades, and the build discovers your applications automatically.

## Layout: one directory per application

```
apps/
  my_app/
    CMakeLists.txt      # required -- picked up automatically
    release.mk          # optional -- opt in to `make release APP=MyApp`
    app_data.toml       # optional -- component manifest for ops tooling
    exec/               # executable target(s)
    scripts/smoke.sh    # optional -- smoke contract (see below)
```

Adding a directory with a `CMakeLists.txt` is the entire integration step:
`apps/CMakeLists.txt` globs its children, and `mk/release.mk` auto-includes
`apps/*/release.mk` manifests alongside the demo manifests.

## Build integration

- **Targets**: use the same helpers the demos use -- `apex_add_app` for
  executables, `apex_add_deployment(NAME <n> EXEC <exec> [TPRM <master>])`
  for a packaged filesystem (bank layout + `run.sh`), `apex_add_bundle` to
  combine deployments with tools and docs. See
  [cmake/apex/README.md](../cmake/apex/README.md).
- **Releases**: a `release.mk` manifest (`APP_REGISTRY += MyApp`,
  `APP_MyApp_PLATFORMS`, per-platform `_TYPE`/`_BINARY`) makes
  `make release APP=MyApp` work. The apex CI release matrix ships only the
  stock demos; user applications release through the same mechanics on
  your side. [template_app/release.mk](template_app/release.mk) is a
  working example.
- **Smoke contract**: an executable `scripts/smoke.sh` receiving
  `APEX_BIN_DIR` runs via `make smoke APP=MyApp`. The build system knows
  only this convention; the script owns app-specific run and verification.
- **Ops tooling**: an `app_data.toml` component manifest feeds
  `make zenith-target APP=MyApp` (searches `demos/` then `apps/`).

## Reserved conventions

The component-spec tooling arc reserves these names inside an application
directory: a component spec file consumed by generators, plus `.auto/`
(generated sources, never hand-edited) and `.ovr/` (user overrides layered
over generated code). Avoid claiming those names for other purposes.

## template_app

[template_app/](template_app/) is a minimal, buildable starting point: one
tunable component (the README front-page pattern), an executive, a release
manifest, and a deployment. Copy it, rename, and go. It also serves as the
in-tree testbed for tunable-parameter tooling changes.
