# ==============================================================================
# mk/tools.mk - Developer utilities and tool builds
#
# Static analysis, profiling, and CLI tool build targets.
# ==============================================================================

ifndef TOOLS_MK_GUARD
TOOLS_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Note: Static analysis runs on native host builds. Cross-compiled and
# bare-metal targets are typically analyzed using the native toolchain.

# Uses BUILD_DIR and NUM_JOBS from common.mk

# Python tools source directory
PY_TOOLS_DIR := tools/py

# ------------------------------------------------------------------------------
# Tool Build Targets
# ------------------------------------------------------------------------------

# _tool_build: Generate a tools-<lang> target that builds via ninja
# $(1) = language suffix (cpp/rust/py), $(2) = display name
define _tool_build
.PHONY: tools-$(1)
tools-$(1): prep
	@test -f "$$(BUILD_DIR)/CMakeCache.txt" || cmake --preset $$(HOST_DEBUG_PRESET) $$(CMAKE_VERBOSE_FLAG)
	@cd "$$(BUILD_DIR)" && if ninja -t query apex_csf_$(1)_tools >/dev/null 2>&1; then \
	  printf '[tools] Building $(2) tools\n'; ninja apex_csf_$(1)_tools; \
	else printf '[tools] No $(2) tools to build\n'; fi
endef

$(eval $(call _tool_build,cpp,C++))
$(eval $(call _tool_build,rust,Rust))
$(eval $(call _tool_build,py,Python))

# Build all tools (C++, Rust, Python)
tools: tools-cpp tools-rust tools-py
	$(call log,tools,All tools built)

# ------------------------------------------------------------------------------
# Apex Data Database (Ops artifacts)
# ------------------------------------------------------------------------------

# Generate struct dictionaries from registered apex_data.toml manifests
# Requires: make tools-rust (for apex_data_gen), cmake configure (for manifest list)
# Output: build/*/apex_data_db/*.json - struct dictionaries for operations systems
apex-data-db: tools-rust
	$(call log,apex-data-db,Generating struct dictionaries)
	@mkdir -p "$(BUILD_DIR)/apex_data_db"
	@if [ ! -f "$(BUILD_DIR)/apex_data_manifests.txt" ]; then \
	  printf '[apex-data-db] No manifest list found - run cmake first\n'; \
	  exit 1; \
	fi
	@if [ ! -s "$(BUILD_DIR)/apex_data_manifests.txt" ]; then \
	  printf '[apex-data-db] No manifests registered\n'; \
	  exit 0; \
	fi
	@while IFS= read -r manifest; do \
	  [ -z "$$manifest" ] && continue; \
	  printf '[apex-data-db] Processing: %s\n' "$$manifest"; \
	  "$(BUILD_DIR)/bin/tools/rust/apex_data_gen" \
	    --manifest "$$manifest" \
	    --output "$(BUILD_DIR)/apex_data_db/" \
	    --pretty; \
	done < "$(BUILD_DIR)/apex_data_manifests.txt"
	$(call log,apex-data-db,Struct dictionaries generated to $(BUILD_DIR)/apex_data_db/)

# ------------------------------------------------------------------------------
# TPRM Templates (Ops artifacts)
# ------------------------------------------------------------------------------

# Generate TOML templates from struct dictionaries for TPRM authoring
# Requires: make apex-data-db (for JSON struct dictionaries)
# Output: build/*/tprm_templates/*.toml - editable templates for TPRM configuration
# Note: Uses Python for JSON parsing (available in container, jq is not)
tprm-templates: apex-data-db
	$(call log,tprm-templates,Generating TOML templates from struct dictionaries)
	@mkdir -p "$(BUILD_DIR)/tprm_templates"
	@for json in "$(BUILD_DIR)/apex_data_db"/*.json; do \
	  [ -f "$$json" ] || continue; \
	  component=$$(basename "$$json" .json); \
	  structs=$$(python3 -c "import json,sys; d=json.load(open('$$json')); print(' '.join(k for k,v in d.get('structs',{}).items() if v.get('category')=='TUNABLE_PARAM'))"); \
	  for struct in $$structs; do \
	    [ -z "$$struct" ] && continue; \
	    outfile="$(BUILD_DIR)/tprm_templates/$${component}_$${struct}.toml"; \
	    printf '[tprm-templates] %s -> %s\n' "$$struct" "$$outfile"; \
	    "$(BUILD_DIR)/bin/tools/rust/tprm_template" \
	      --json "$$json" \
	      --struct "$$struct" \
	      --output "$$outfile"; \
	  done; \
	done
	$(call log,tprm-templates,TOML templates generated to $(BUILD_DIR)/tprm_templates/)

# ------------------------------------------------------------------------------
# Ops Command/Telemetry Deck
# ------------------------------------------------------------------------------

# Generate a consolidated cmd/tlm deck from struct dictionaries.
# Produces a single markdown document describing all commands, telemetry,
# tunable parameters, and state data across all registered components.
#
# Requires: make apex-data-db (for JSON struct dictionaries)
# Output: build/*/ops_deck.md
ops-deck: apex-data-db
	$(call log,ops-deck,Generating command/telemetry deck)
	@python3 $(PY_TOOLS_DIR)/src/apex_tools/ops/deck_gen.py \
	  --db "$(BUILD_DIR)/apex_data_db" \
	  --output "$(BUILD_DIR)/ops_deck.md"

# ------------------------------------------------------------------------------
# Ops Artifacts (umbrella)
# ------------------------------------------------------------------------------

# Generate all Operations integration artifacts in a single command.
# Produces struct dictionaries, TPRM templates, and the cmd/tlm deck.
#
# Output:
#   build/*/apex_data_db/*.json       Struct dictionaries (field layouts)
#   build/*/tprm_templates/*.toml     Editable TPRM configuration templates
#   build/*/ops_deck.md                Consolidated command/telemetry deck
ops-artifacts: apex-data-db tprm-templates ops-deck
	$(call log,ops-artifacts,All Ops artifacts generated to $(BUILD_DIR)/)

# ------------------------------------------------------------------------------
# Ops SDK Package
# ------------------------------------------------------------------------------

# Bundles struct dictionaries, runtime metadata exports, and a quick-start
# README into a self-contained package for operations integrators.
#
# Usage:
#   make ops-sdk APP=ApexHilDemo
#
# Requires:
#   - make apex-data-db (struct dictionaries)
#   - A prior test run (for .apex_fs/db/ exports -- optional but recommended)
#
# Output:
#   build/*/ops_sdk/<APP>-ops-sdk.tar.gz
#     <APP>/structs/*.json          Struct dictionaries (type layouts)
#     <APP>/runtime/registry.rdat   Component/task/data metadata (if available)
#     <APP>/runtime/scheduler.sdat  Task schedule metadata (if available)
#     <APP>/README.md               Quick-start guide for operations integrators

ops-sdk: apex-data-db
	@test -n "$(APP)" || { printf '$(TERM_RED)[ops-sdk]$(TERM_RESET) APP not set. Usage: make ops-sdk APP=<name>\n'; exit 1; }
	$(call log,ops-sdk,Packaging Ops SDK for $(APP))
	@bash tools/sh/bin/ops_sdk_package.sh \
	  --app "$(APP)" \
	  --build-dir "$(BUILD_DIR)"

# ------------------------------------------------------------------------------
# Zenith Target Config Generation
# ------------------------------------------------------------------------------

# Generate Zenith target configs from app_data.toml + struct dictionaries.
# Reads the per-app app_data.toml manifest and combines it with the JSON
# struct dictionaries to produce a ready-to-use target directory.
#
# Usage:
#   make zenith-target APP=ApexOpsDemo
#
# Requires:
#   - make apex-data-db (for JSON struct dictionaries)
#   - apps/<app>/app_data.toml (per-app component manifest)
#
# Output:
#   build/*/zenith_targets/<APP>/
#     app_manifest.json   Component list + protocol config
#     commands.json       Opcode table for command panel
#     telemetry.json      Default telemetry plot layouts
#     structs/*.json      Struct dictionaries (copied)

zenith-target: apex-data-db
	@test -n "$(APP)" || { printf '$(TERM_RED)[zenith-target]$(TERM_RESET) APP not set. Usage: make zenith-target APP=<name>\n'; exit 1; }
	$(call log,zenith-target,Generating Zenith target configs for $(APP))
	@python3 $(PY_TOOLS_DIR)/src/apex_tools/ops/target_gen.py \
	  --app "$(APP)" \
	  --apps-dir "apps" \
	  --db "$(BUILD_DIR)/apex_data_db" \
	  --output "$(BUILD_DIR)/zenith_targets/$(APP)"

# Validate generated target config against a live Apex target.
# Connects via APROTO, queries GET_REGISTRY + GET_DATA_CATALOG, and
# diffs against the generated config. Warns on mismatches.
#
# Usage:
#   make zenith-validate APP=ApexOpsDemo HOST=192.168.1.119
#   make zenith-validate APP=ApexOpsDemo HOST=localhost PORT=9000

zenith-validate: zenith-target
	@test -n "$(HOST)" || { printf '$(TERM_RED)[zenith-validate]$(TERM_RESET) HOST not set. Usage: make zenith-validate APP=<name> HOST=<ip> [PORT=<port>]\n'; exit 1; }
	$(call log,zenith-validate,Validating $(APP) against $(HOST):$(or $(PORT),9000))
	@python3 $(PY_TOOLS_DIR)/src/apex_tools/ops/target_validate.py \
	  --generated "$(BUILD_DIR)/zenith_targets/$(APP)" \
	  --host "$(HOST)" \
	  --port "$(or $(PORT),9000)"

# ------------------------------------------------------------------------------
# Static Analysis
# ------------------------------------------------------------------------------

# Static analysis with Clang's scan-build
static: prep
	$(call log,static,Configuring for static analysis)
	@cmake -DCMAKE_BUILD_TYPE=Debug -B"$(BUILD_DIR)" -S. -GNinja
	$(call log,static,Running scan-build)
	@cd "$(BUILD_DIR)" && scan-build --status-bugs ninja -j$(NUM_JOBS)
	$(call log,static,Running tests to verify)
	@cd "$(BUILD_DIR)" && $(call with_lib_path,ctest --output-on-failure)

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: static tools apex-data-db tprm-templates ops-deck ops-artifacts ops-sdk zenith-target zenith-validate

endif  # TOOLS_MK_GUARD
