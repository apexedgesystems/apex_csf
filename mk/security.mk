# ==============================================================================
# mk/security.mk - Supply-chain, secret, and SAST scanners
#
# Each target runs a scanner over the working tree inside the dev container, so
# local and CI runs are identical.
#
# Gating policy (tiered by determinism):
#   - gitleaks ALWAYS gates. Secrets are a hard line and the .gitleaks.toml
#     allowlist keeps it deterministic, so it is safe on the PR gate. Runs on
#     both the PR gate and nightly.
#   - trivy / osv / semgrep are ADVISORY by default (exit 0 on findings): they
#     report but never block local work or the fast PR gate. Their rule sets and
#     CVE databases drift upstream, so hard-gating the PR on them would make the
#     gate non-deterministic and a tagged release non-reproducible -- the
#     opposite of what flight-code assurance wants. They GATE when invoked with
#     SCAN_GATE=1, which nightly sets, so a newly published CVE or finding fails
#     the nightly job (surfaced for triage) without ever blocking a PR.
#
#   make trivy      vulnerabilities + secrets + misconfig (filesystem)
#   make sbom       CycloneDX software bill of materials (trivy)
#   make gitleaks   secret scanning (always gating)
#   make osv        dependency vulnerabilities (OSV database)
#   make semgrep    pattern-based SAST
#
# Gate the advisory scanners (nightly does this):  make osv SCAN_GATE=1
# ==============================================================================

ifndef SECURITY_MK_GUARD
SECURITY_MK_GUARD := 1

SECURITY_DIR ?= build/security

$(SECURITY_DIR):
	@mkdir -p $(SECURITY_DIR)

# SCAN_GATE=1 flips the advisory scanners (trivy/osv/semgrep) to fail on
# findings; default 0 keeps them report-only for local runs and the PR gate.
SCAN_GATE ?= 0
ifeq ($(SCAN_GATE),1)
  _trivy_exit  := --exit-code 1
  _osv_tail    :=
  _semgrep_err := --error
else
  _trivy_exit  :=
  _osv_tail    := || true
  _semgrep_err :=
endif

# trivy: vuln + secret + misconfig over the filesystem. Advisory unless
# SCAN_GATE=1 adds --exit-code 1. build/ is skipped (artifacts).
trivy:
	$(call log,trivy,Scanning filesystem -- vulnerabilities, secrets, misconfig)
	@trivy fs --scanners vuln,secret,misconfig --no-progress --skip-dirs build $(_trivy_exit) .

# sbom: CycloneDX bill of materials for supply-chain provenance.
sbom: | $(SECURITY_DIR)
	$(call log,sbom,Generating CycloneDX SBOM)
	@trivy fs --format cyclonedx --output $(SECURITY_DIR)/sbom.cdx.json --skip-dirs build .
	$(call log_ok,sbom,wrote $(SECURITY_DIR)/sbom.cdx.json)

# gitleaks: secret scanning. GATING -- clean today via the .gitleaks.toml
# allowlist (crypto test vectors), so it only fails on a newly introduced
# secret. Runs on the PR gate and nightly.
gitleaks:
	$(call log,gitleaks,Scanning for secrets)
	@gitleaks detect --source . --no-banner --redact

# osv-scanner: dependency CVEs from lockfiles (Cargo.lock, poetry, ...). Respects
# .gitignore, so build/ vendored crates are skipped. Advisory (trailing
# `|| true`) unless SCAN_GATE=1 drops it so a CVE fails the job.
osv:
	$(call log,osv,Scanning dependencies against the OSV database)
	@osv-scanner scan source --recursive . $(_osv_tail)

# semgrep: pattern SAST. Advisory unless SCAN_GATE=1 adds --error to gate.
semgrep:
	$(call log,semgrep,Running pattern-based SAST)
	@semgrep scan --config auto --quiet $(_semgrep_err) .

.PHONY: trivy sbom gitleaks osv semgrep

endif  # SECURITY_MK_GUARD
