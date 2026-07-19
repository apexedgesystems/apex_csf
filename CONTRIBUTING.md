# Contributing to Apex CSF

This guide covers setting up a development environment, running the
quality gates a change must pass, and shaping a pull request.

## Development Environment

Docker is the recommended path -- every toolchain, dependency, and
analysis tool is pre-installed in the images, and CI runs the same ones:

```bash
make shell-dev        # interactive shell in the dev container
make compose-debug    # or: build directly from the host
```

For native builds you need a C++23 compiler (Clang 21 recommended),
CMake 3.24+, and Ninja; see the README's Requirements section for the
optional extras.

## Build and Test

```bash
make compose-debug     # configure + build (debug preset)
make compose-testp     # run the C++ test suite in parallel
make test-rust         # Rust CLI tool tests
make test-py           # Python CLI tool tests
make compose-coverage  # coverage build + per-language HTML reports
```

`make help` lists every target, including per-platform cross builds and
firmware.

## Quality Gates

The PR gate runs these; run them locally first:

```bash
make format-check   # formatting (C++, CMake, shell, Markdown, YAML)
make static         # clang-tidy static analysis (C++)
make clippy         # clippy, deny warnings (Rust tools)
make compose-testp  # tests
```

Tool changes also run their family's suite: `make test-rust`,
`make test-py`, or `make test-sh`.

`make format` applies formatting fixes in place. Static analysis and
tests run inside the same container images CI uses, so a local pass is
representative.

## Pull Requests

- Keep PRs small and themed; unrelated changes belong in separate PRs.
- All gates green, and new behavior comes with tests.
- Update documentation with the code: the affected module README, and a
  `CHANGELOG.md` entry under Unreleased for user-facing changes.
- Commit messages describe the change as present fact; PR descriptions
  describe the diff.

## Versioning and Releases

Versions follow semantic versioning, with a pre-1.0 caveat: minor
releases may contain breaking changes, and every breaking change is
called out in `CHANGELOG.md`. Releases are cut from tags by maintainers;
artifacts publish to the GitHub Releases page.

## Security Issues

Never report vulnerabilities through public issues -- see
[SECURITY.md](SECURITY.md) for the private reporting flow.
