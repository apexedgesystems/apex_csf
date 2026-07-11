# Security Policy

## Reporting a Vulnerability

Report suspected vulnerabilities privately through
[GitHub private vulnerability reporting](https://github.com/apexedgesystems/apex_csf/security/advisories/new).
Do not open public issues, discussions, or pull requests for security
reports.

A useful report includes the affected component and commit or release,
the impact as you understand it, and reproduction steps or a proof of
concept.

You will receive an acknowledgment within 7 days. Disclosure is
coordinated: we ask that you hold publication until a fix has shipped in
a release, and we credit reporters in the advisory unless they prefer
otherwise.

## Supported Versions

| Version        | Supported                |
| -------------- | ------------------------ |
| Latest release | Yes                      |
| `main`         | Yes (development head)   |
| Older releases | No -- upgrade to latest  |

While the major version is 0, fixes land on `main` and ship in the next
release; there are no backport branches.

## Scope

In scope: code in this repository, the published container images, and
release artifacts. For vulnerabilities in third-party dependencies,
report upstream first; the repository tracks dependency advisories
through scheduled scans and automated update PRs.
