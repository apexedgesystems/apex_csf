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
#   make notices    THIRD_PARTY_NOTICES.md derived from the SBOM
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

# notices: THIRD_PARTY_NOTICES.md derived from the SBOM, so the inventory can
# never disagree with it. Lockfile-derived SBOM components carry no license
# text, so the license column is enriched from `cargo metadata` (offline: it
# reads the manifests in the image's baked registry cache); components it
# cannot resolve say so honestly. The generator is python3 (present in every
# dev tier and on bare CI runners) rather than jq, which the images lack.
define _NOTICES_PY
import json, sys

sbom_path, cargo_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
with open(sbom_path) as f:
    sbom = json.load(f)
cargo_lic = {}
try:
    with open(cargo_path) as f:
        for pkg in json.load(f).get("packages", []):
            cargo_lic[(pkg["name"], pkg["version"])] = pkg.get("license")
except (OSError, ValueError):
    pass
rows = []
for comp in sbom.get("components", []):
    name = comp.get("name", "?")
    version = comp.get("version", "?")
    purl = comp.get("purl", "")
    eco = purl.split(":", 1)[1].split("/", 1)[0] if purl.startswith("pkg:") else "-"
    names = []
    for entry in comp.get("licenses", []):
        lic = entry.get("license", {})
        text = lic.get("id") or lic.get("name") or entry.get("expression")
        if text:
            names.append(text)
    if not names and eco == "cargo" and cargo_lic.get((name, version)):
        names.append(cargo_lic[(name, version)])
    lic_text = ", ".join(sorted(set(names))) or "see upstream"
    rows.append((name, version, eco, lic_text))
rows.sort()
with open(out_path, "w") as f:
    f.write("# Third-Party Notices\n\n")
    f.write("Third-party components consumed by this repository, derived\n")
    f.write("from the CycloneDX SBOM (`make sbom`) with license text\n")
    f.write("resolved from package manifests. Each component remains under\n")
    f.write("its own license.\n\n")
    f.write("| Component | Version | Ecosystem | License |\n")
    f.write("| --------- | ------- | --------- | ------- |\n")
    for name, version, eco, lic in rows:
        f.write("| " + name + " | " + version + " | " + eco + " | " + lic + " |\n")
resolved = sum(1 for r in rows if r[3] != "see upstream")
print(str(len(rows)) + " components, " + str(resolved) + " with resolved licenses")
endef
export _NOTICES_PY

notices: sbom
	$(call log,notices,Deriving THIRD_PARTY_NOTICES.md from the SBOM)
	@cd tools/rust && cargo metadata --locked --format-version 1 \
	  > ../../$(SECURITY_DIR)/cargo-metadata.json 2>/dev/null \
	  || echo '{}' > ../../$(SECURITY_DIR)/cargo-metadata.json
	@python3 -c "$$_NOTICES_PY" $(SECURITY_DIR)/sbom.cdx.json \
	  $(SECURITY_DIR)/cargo-metadata.json $(SECURITY_DIR)/THIRD_PARTY_NOTICES.md
	$(call log_ok,notices,wrote $(SECURITY_DIR)/THIRD_PARTY_NOTICES.md)

# gitleaks: secret scanning. GATING -- clean today via the .gitleaks.toml
# allowlist (crypto test vectors), so it only fails on a newly introduced
# secret. Runs on the PR gate and nightly.
gitleaks:
	$(call log,gitleaks,Scanning for secrets)
	@gitleaks detect --source . --no-banner --redact

# osv-scanner: dependency CVEs from lockfiles (Cargo.lock, uv, ...). Respects
# .gitignore, so build/ vendored crates are skipped. Advisory (trailing
# `|| true`) unless SCAN_GATE=1 drops it so a CVE fails the job.
osv:
	$(call log,osv,Scanning dependencies against the OSV database)
	@osv-scanner scan source --recursive . $(_osv_tail)

# semgrep: pattern SAST. Advisory unless SCAN_GATE=1 adds --error to gate.
semgrep:
	$(call log,semgrep,Running pattern-based SAST)
	@semgrep scan --config auto --quiet $(_semgrep_err) .

.PHONY: trivy sbom notices gitleaks osv semgrep

endif  # SECURITY_MK_GUARD
