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
# Apex Data Database (C2 artifacts)
# ------------------------------------------------------------------------------

# Generate struct dictionaries from registered apex_data.toml manifests
# Requires: make tools-rust (for apex_data_gen), cmake configure (for manifest list)
# Output: build/*/apex_data_db/*.json - struct dictionaries for C2 systems
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
# TPRM Templates (C2 artifacts)
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
# C2 Command/Telemetry Deck
# ------------------------------------------------------------------------------

# Generate a consolidated cmd/tlm deck from struct dictionaries.
# Produces a single markdown document describing all commands, telemetry,
# tunable parameters, and state data across all registered components.
#
# Requires: make apex-data-db (for JSON struct dictionaries)
# Output: build/*/c2_deck.md
c2-deck: apex-data-db
	$(call log,c2-deck,Generating command/telemetry deck)
	@python3 $(PY_TOOLS_DIR)/src/apex_tools/c2/deck_gen.py \
	  --db "$(BUILD_DIR)/apex_data_db" \
	  --output "$(BUILD_DIR)/c2_deck.md"

# ------------------------------------------------------------------------------
# C2 Artifacts (umbrella)
# ------------------------------------------------------------------------------

# Generate all C2 integration artifacts in a single command.
# Produces struct dictionaries, TPRM templates, and the cmd/tlm deck.
#
# Output:
#   build/*/apex_data_db/*.json       Struct dictionaries (field layouts)
#   build/*/tprm_templates/*.toml     Editable TPRM configuration templates
#   build/*/c2_deck.md                Consolidated command/telemetry deck
c2-artifacts: apex-data-db tprm-templates c2-deck
	$(call log,c2-artifacts,All C2 artifacts generated to $(BUILD_DIR)/)

# ------------------------------------------------------------------------------
# C2 SDK Package
# ------------------------------------------------------------------------------

# Bundles struct dictionaries, runtime metadata exports, and a quick-start
# README into a self-contained package for C2 integrators.
#
# Usage:
#   make c2-sdk APP=ApexHilDemo
#
# Requires:
#   - make apex-data-db (struct dictionaries)
#   - A prior test run (for .apex_fs/db/ exports -- optional but recommended)
#
# Output:
#   build/*/c2_sdk/<APP>-c2-sdk.tar.gz
#     <APP>/structs/*.json          Struct dictionaries (type layouts)
#     <APP>/runtime/registry.rdat   Component/task/data metadata (if available)
#     <APP>/runtime/scheduler.sdat  Task schedule metadata (if available)
#     <APP>/README.md               Quick-start guide for C2 integrators

c2-sdk: apex-data-db
	@test -n "$(APP)" || { printf '$(TERM_RED)[c2-sdk]$(TERM_RESET) APP not set. Usage: make c2-sdk APP=<name>\n'; exit 1; }
	$(call log,c2-sdk,Packaging C2 SDK for $(APP))
	@bash tools/sh/bin/c2_sdk_package.sh \
	  --app "$(APP)" \
	  --build-dir "$(BUILD_DIR)"

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

.PHONY: static tools apex-data-db tprm-templates c2-deck c2-artifacts c2-sdk

endif  # TOOLS_MK_GUARD
