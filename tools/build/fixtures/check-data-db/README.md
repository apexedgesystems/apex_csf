# check-data-db contract fixtures

Three registered-list entries `make check-data-db-contract` feeds to the real
`check-data-db` recipe, one list per run, each with its required outcome:

| Fixture         | Outcome the gate must produce                                |
| --------------- | ------------------------------------------------------------ |
| `valid`         | exit 0, "1 registered dictionaries generate clean"           |
| `broken-header` | nonzero exit, the generator's "declares header" error        |
| `warning`       | nonzero exit, "a dictionary above carries a \_warning entry" |

The warning fixture declares an enum no header defines beside an `inc/`
header that discovery does parse, so the generator emits the dictionary with
a `_warning` entry instead of failing at a declared path.
