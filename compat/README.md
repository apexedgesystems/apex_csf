# compat/ -- cross-implementation conformance contracts

Where two independent implementations must agree on bytes, the
agreement lives here as data -- golden vectors and, as they arrive,
format specifications -- and every implementation answers to the
contract, never to another implementation's source code.

This is the implementation-level sibling of the per-library
`lib.manifest` support contracts: manifests verify a library's platform
promises; compat contracts verify that separate codebases speak the
same formats.

## Contracts

| Contract | Sides in this repo | External consumers |
| -------- | ------------------ | ------------------ |
| [tprm/](tprm/) | rust tooling (generator), C++ runtime (readers) | Zenith C2 backend |

## Running the checks

`make compat` runs every contract's in-repo suites; `make
compat-<name>` runs one (e.g. `make compat-tprm`). The CI gate runs the
same suites whenever a contract or one of its sides changes.

External consumers copy a contract directory into their own tree and
test against the copy -- repositories stay independent; the bytes are
the interface.

## Adding a contract

A directory here with a README naming every side, the contract data,
and a pinning test in each in-repo side; a `compat-<name>` make target
aggregating those tests; a classifier entry in ci.yml scoping the
directory to the sides' suites.
