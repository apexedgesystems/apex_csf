# template_app

Minimal user application: one schedulable component with tunable
parameters, wired into an executive. This is the README front-page
pattern, buildable -- copy the directory, rename `TemplateApp`, and start
replacing the thermal model with your components.

It also serves as the in-tree testbed for tunable-parameter tooling: a
small, stable `TParams` struct that TPRM generation and upload changes
can be exercised against.

## Build and run

```bash
make compose-debug                      # builds TemplateApp with everything else
./build/hosted-x86_64-debug/apps/template_app/exec/TemplateApp
```

## Release

`release.mk` registers the app with the manifest surface:

```bash
make release APP=TemplateApp
```
