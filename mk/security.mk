# ==============================================================================
# mk/security.mk - Supply-chain, secret, and SAST scanners
#
# Each target runs a scanner over the working tree inside the dev container, so
# local and CI runs are identical. All are ADVISORY for now: they report but do
# not fail the build. To gate one, see the per-target note (drop the trailing
# `|| true`, or add the tool's error-exit flag).
#
#   make trivy      vulnerabilities + secrets + misconfig (filesystem)
#   make sbom       CycloneDX software bill of materials (trivy)
#   make gitleaks   secret scanning
#   make osv        dependency vulnerabilities (OSV database)
#   make semgrep    pattern-based SAST
# ==============================================================================

ifndef SECURITY_MK_GUARD
SECURITY_MK_GUARD := 1

SECURITY_DIR ?= build/security

$(SECURITY_DIR):
	@mkdir -p $(SECURITY_DIR)

# trivy: vuln + secret + misconfig over the filesystem. Exits 0 unless
# --exit-code is set, so it is advisory by default. build/ is skipped (artifacts).
trivy:
	$(call log,trivy,Scanning filesystem -- vulnerabilities, secrets, misconfig)
	@trivy fs --scanners vuln,secret,misconfig --no-progress --skip-dirs build .

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
# .gitignore, so build/ vendored crates are skipped. `|| true` keeps it advisory.
osv:
	$(call log,osv,Scanning dependencies against the OSV database)
	@osv-scanner scan source --recursive . || true

# semgrep: pattern SAST. Default exit is 0 (advisory); add --error to gate.
semgrep:
	$(call log,semgrep,Running pattern-based SAST)
	@semgrep scan --config auto --quiet .

.PHONY: trivy sbom gitleaks osv semgrep

endif  # SECURITY_MK_GUARD
