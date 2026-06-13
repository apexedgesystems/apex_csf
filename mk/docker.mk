# ==============================================================================
# mk/docker.mk - Container builds and management
#
# Wrapper around docker compose for building development shells, builder images,
# and extracting release artifacts.
#
# Consolidated layout:
#   base.Dockerfile        - Shared tools, compilers, user setup
#   dev/<target>.Dockerfile - Dev shell (includes toolchain directly)
#   builder.Dockerfile     - Unified parameterized builder
#   final.Dockerfile       - Artifact packager
# ==============================================================================

ifndef DOCKER_MK_GUARD
DOCKER_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

# Enable BuildKit for cache mounts and improved layer caching
export DOCKER_BUILDKIT := 1
export COMPOSE_DOCKER_CLI_BUILD := 1

# Export host identity for docker compose.
# UID/GID are bash readonly builtins, so we use HOST_UID/HOST_GID to avoid collision.
export USER     := $(shell id -un)
export HOST_UID := $(shell id -u)
export HOST_GID := $(shell id -g)
# Host's running kernel -- the dev image installs the matching linux-tools so
# `perf` works (perf must match the running kernel; linux-tools-generic drifts).
export HOST_KERNEL := $(shell uname -r)

# Artifact output directory
DOCKER_OUT_DIR := output

# Project version (extracted from CMakeLists.txt for tarball naming).
# Override at the command line: make artifacts VERSION=1.2.3
VERSION ?= $(shell sed -n 's/.*VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt | head -1)

# Registry for dev image caching (ghcr.io/<owner>/<repo>).
# Set in CI via environment; not required for local builds.
REGISTRY ?=

# ------------------------------------------------------------------------------
# Dev Image Registry (single source of truth)
# ------------------------------------------------------------------------------
# Every dev shell service. The image name is apex.dev.<suffix> (suffix is 'cpu'
# for the base 'dev' service, else the text after 'dev-'). The base image each
# one builds FROM differs:
#   dev, dev-cuda  -> base
#   dev-jetson     -> dev-cuda  (aarch64 + CUDA cross)
#   all others     -> dev       (CPU)
#
# The build target list (docker-devs), the per-service build/shell targets, and
# the registry push/pull list are all derived from this -- add a platform once.
DEV_SERVICES := dev dev-cuda dev-jetson dev-rpi dev-riscv64 dev-zephyr \
                dev-stm32 dev-esp32 dev-pico dev-arduino dev-atmega328pb \
                dev-pic32 dev-c2000

# Release builder targets: one apex.builder.<t> image per entry. The
# docker-builders aggregate, the per-target build rules, the release.yml
# builders matrix (via print-builder-targets), the artifact-<t> extraction
# targets, and the check-release-paths COPY coverage are all derived from
# this -- add a target once (plus its ARTIFACT_NAME/_DIR pair below).
BUILDER_TARGETS := cpu cuda jetson rpi riscv64 stm32 arduino pico esp32 c2000

# Per-target artifact naming and build tree, mirroring final.Dockerfile's
# COPY sources and tarball names exactly (check-release-paths asserts the
# dirs against CMakePresets; the names feed apex-csf-<VERSION>-<name>.tar.gz).
ARTIFACT_NAME_cpu     := x86_64-linux
ARTIFACT_NAME_cuda    := x86_64-linux-cuda
ARTIFACT_NAME_jetson  := aarch64-jetson
ARTIFACT_NAME_rpi     := aarch64-rpi
ARTIFACT_NAME_riscv64 := riscv64-linux
ARTIFACT_NAME_stm32   := stm32
ARTIFACT_NAME_arduino := arduino
ARTIFACT_NAME_pico    := pico
ARTIFACT_NAME_esp32   := esp32
ARTIFACT_NAME_c2000   := c2000

ARTIFACT_DIR_cpu     := hosted-x86_64-release
ARTIFACT_DIR_cuda    := hosted-x86_64-release
ARTIFACT_DIR_jetson  := cross-jetson-release
ARTIFACT_DIR_rpi     := cross-rpi-release
ARTIFACT_DIR_riscv64 := cross-riscv64-release
ARTIFACT_DIR_stm32   := mcu-stm32-relwithdebinfo
ARTIFACT_DIR_arduino := mcu-arduino-relwithdebinfo
ARTIFACT_DIR_pico    := mcu-pico-relwithdebinfo
ARTIFACT_DIR_esp32   := mcu-esp32-relwithdebinfo
ARTIFACT_DIR_c2000   := mcu-c2000-relwithdebinfo

# _dev_base: the base image a dev service builds from
_dev_base = $(if $(filter dev dev-cuda,$(1)),docker-base,$(if $(filter dev-jetson,$(1)),docker-dev-cuda,docker-dev))

# _dev_image: image name for a dev service (apex.dev.cpu for 'dev', else apex.dev.<x>)
_dev_image = apex.dev.$(if $(filter dev,$(1)),cpu,$(patsubst dev-%,%,$(1)))

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# _docker_build: Build a docker compose service
# Usage: $(call _docker_build,service)
define _docker_build
	$(call log,docker,Building $(1))
	@docker compose build $(1)
endef

# _docker_shell: Run an interactive shell in a docker compose service
# Usage: $(call _docker_shell,service)
define _docker_shell
	@docker compose run --rm $(1)
endef

# ------------------------------------------------------------------------------
# Target Templates
# ------------------------------------------------------------------------------

# _dev_target: Generate docker-<service> build target
# $(1) = compose service name, $(2) = prerequisite target
define _dev_target
.PHONY: docker-$(1)
docker-$(1): $(2)
	$$(call _docker_build,$(1))
endef

# _shell_target: Generate shell-<service> interactive target
# $(1) = compose service name
define _shell_target
.PHONY: shell-$(1)
shell-$(1): docker-$(1)
	$$(call _docker_shell,$(1))
endef

# _builder_target: Generate docker-builder-<name> build target.
# $(1) = builder suffix
#
# Builders compile the apex source against a dev image (e.g. apex.builder.cpu
# uses apex.dev.cpu as its FROM). The dev image is treated as an external
# input: builder targets do NOT carry docker-<dev> as a prerequisite. Callers
# must ensure the required dev image is in the local docker daemon, either by:
#
#   - running `make docker-devs` first, or
#   - pulling from the registry before invoking make (CI flow).
#
# This avoids the wasteful dev-image rebuild that would otherwise happen on
# every `make artifacts` invocation in CI, where the dev images are already
# pulled from ghcr.io. A first-time local build should use `make docker-all`,
# which orders the dev/builder builds correctly.
define _builder_target
.PHONY: docker-builder-$(1)
docker-builder-$(1):
	$$(call _docker_build,builder-$(1))
endef

# ------------------------------------------------------------------------------
# Aggregate Targets
# ------------------------------------------------------------------------------

# Top-down build for local developers starting from scratch. Each phase is
# invoked recursively so the stages serialize even under parallel make:
# builders no longer carry transitive dev-image prerequisites (see
# _builder_target), so the explicit ordering belongs here.
docker-all:
	@$(MAKE) docker-base
	@$(MAKE) docker-devs
	@$(MAKE) docker-builders
	@$(MAKE) docker-final
	$(call log,docker,All images built)

docker-devs: $(foreach s,$(DEV_SERVICES),docker-$(s))
	$(call log,docker,All dev images built)

docker-builders: $(patsubst %,docker-builder-%,$(BUILDER_TARGETS))
	$(call log,docker,All builder images built)

# ------------------------------------------------------------------------------
# Base Image
# ------------------------------------------------------------------------------

docker-base:
	$(call _docker_build,base)

# ------------------------------------------------------------------------------
# Development Shell Images (generated from templates)
# ------------------------------------------------------------------------------
# Dependency structure (see _dev_base):
#   base -> dev, dev-cuda
#   dev-cuda -> dev-jetson
#   dev (CPU) -> all other dev images

$(foreach s,$(DEV_SERVICES),\
  $(eval $(call _dev_target,$(s),$(call _dev_base,$(s)))))

# ------------------------------------------------------------------------------
# Builder Images (generated from templates)
# ------------------------------------------------------------------------------

$(foreach b,$(BUILDER_TARGETS),\
  $(eval $(call _builder_target,$(b))))

# ------------------------------------------------------------------------------
# Interactive Shells (generated from templates)
# ------------------------------------------------------------------------------

$(foreach s,$(DEV_SERVICES),$(eval $(call _shell_target,$(s))))

# ------------------------------------------------------------------------------
# Final Packager
# ------------------------------------------------------------------------------

# final COPYs from all 10 builder images. Like the builders treat their dev
# image as an external input, final treats the builder images as external (no
# docker-builders prerequisite), so CI can build the builders on separate
# runners (release.yml) and have a downstream job assemble final from the pulled
# images without rebuilding them. Local first-time builds use `make docker-all`
# (or run `make docker-builders` first), which orders the stages explicitly.
docker-final:
	$(call log,docker,Building final image (VERSION=$(VERSION)))
	@docker compose build --build-arg VERSION=$(VERSION) final

# ------------------------------------------------------------------------------
# Artifact Extraction
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
# Per-target artifact extraction (no final image required)
# ------------------------------------------------------------------------------
# artifact-<t> pulls one platform's tarball straight out of apex.builder.<t>,
# producing the same name and layout as the final-image path below. CI release
# builders use this so every tree is tarred on the runner that built it -- the
# fan-in never materializes ten build trees on one disk (run 27390083508
# filled a 145 GB runner doing exactly that). `make artifacts` (final image +
# extraction) remains the local all-at-once path.

define _artifact_target
.PHONY: artifact-$(1)
artifact-$(1):
	$$(call log,docker,Extracting $(1) artifact (VERSION=$$(VERSION)))
	@mkdir -p $$(DOCKER_OUT_DIR)/.stage-$(1)
	@CID=$$$$(docker create apex.builder.$(1) noop) && \
	  docker cp "$$$$CID:/home/$$(USER)/workspace/build/$$(ARTIFACT_DIR_$(1))" \
	    "$$(DOCKER_OUT_DIR)/.stage-$(1)/$(1)" && \
	  tar -czf "$$(DOCKER_OUT_DIR)/apex-csf-$$(VERSION)-$$(ARTIFACT_NAME_$(1)).tar.gz" \
	    -C "$$(DOCKER_OUT_DIR)/.stage-$(1)" "./$(1)" && \
	  docker rm "$$$$CID" >/dev/null && rm -rf "$$(DOCKER_OUT_DIR)/.stage-$(1)"
	$$(call log_ok,docker,apex-csf-$$(VERSION)-$$(ARTIFACT_NAME_$(1)).tar.gz)
endef

$(foreach t,$(BUILDER_TARGETS),$(eval $(call _artifact_target,$(t))))

# CLI tools + python wheel ride the cpu builder (same sources final.Dockerfile
# collects from apex.builder.cpu).
.PHONY: artifact-tools
artifact-tools:
	$(call log,docker,Extracting tools artifact (VERSION=$(VERSION)))
	@mkdir -p $(DOCKER_OUT_DIR)/.stage-tools
	@CID=$$(docker create apex.builder.cpu noop) && \
	  docker cp "$$CID:/home/$(USER)/workspace/build/$(ARTIFACT_DIR_cpu)/bin/tools" \
	    "$(DOCKER_OUT_DIR)/.stage-tools/tools-bin" && \
	  docker cp "$$CID:/home/$(USER)/workspace/build/$(ARTIFACT_DIR_cpu)/apex_csf-wheels" \
	    "$(DOCKER_OUT_DIR)/.stage-tools/tools-py" && \
	  tar -czf "$(DOCKER_OUT_DIR)/apex-tools-$(VERSION)-x86_64-linux.tar.gz" \
	    -C "$(DOCKER_OUT_DIR)/.stage-tools" "./tools-bin" && \
	  cp "$(DOCKER_OUT_DIR)"/.stage-tools/tools-py/*.whl \
	    "$(DOCKER_OUT_DIR)/apex_py_tools-$(VERSION)-py3-none-any.whl" && \
	  docker rm "$$CID" >/dev/null && rm -rf "$(DOCKER_OUT_DIR)/.stage-tools"
	$(call log_ok,docker,apex-tools-$(VERSION)-x86_64-linux.tar.gz + wheel)

artifacts: docker-final
	$(call log,docker,Extracting artifacts to $(DOCKER_OUT_DIR)/ (VERSION=$(VERSION)))
	@mkdir -p $(DOCKER_OUT_DIR)
	@CID=$$(docker create apex.final) && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-x86_64-linux.tar.gz      $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-x86_64-linux-cuda.tar.gz $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-aarch64-jetson.tar.gz    $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-aarch64-rpi.tar.gz       $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-riscv64-linux.tar.gz     $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-stm32.tar.gz             $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-arduino.tar.gz           $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-pico.tar.gz              $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-esp32.tar.gz             $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-csf-$(VERSION)-c2000.tar.gz             $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex-tools-$(VERSION)-x86_64-linux.tar.gz    $(DOCKER_OUT_DIR)/ && \
	  docker cp $$CID:/output/apex_py_tools-$(VERSION)-py3-none-any.whl    $(DOCKER_OUT_DIR)/ && \
	  docker rm $$CID
	$(call log,docker,Artifacts ready: $(DOCKER_OUT_DIR)/)

# ------------------------------------------------------------------------------
# Registry Push / Pull (dev images only -- builders are never cached)
# ------------------------------------------------------------------------------

_DEV_IMAGES := apex.base $(foreach s,$(DEV_SERVICES),$(call _dev_image,$(s)))

docker-push-devs: docker-devs
	@test -n "$(REGISTRY)" || \
	  { printf '$(TERM_RED)[docker]$(TERM_RESET) REGISTRY is not set. Use: make docker-push-devs REGISTRY=ghcr.io/owner/repo\n'; exit 1; }
	$(call log,docker,Pushing dev images to $(REGISTRY))
	@for img in $(_DEV_IMAGES); do \
	  docker tag $$img $(REGISTRY)/$$img:latest && \
	  docker push $(REGISTRY)/$$img:latest || exit 1; \
	done
	$(call log_ok,docker,Dev images pushed to $(REGISTRY))

docker-pull-devs:
	@test -n "$(REGISTRY)" || \
	  { printf '$(TERM_RED)[docker]$(TERM_RESET) REGISTRY is not set. Use: make docker-pull-devs REGISTRY=ghcr.io/owner/repo\n'; exit 1; }
	$(call log,docker,Pulling dev images from $(REGISTRY))
	@for img in $(_DEV_IMAGES); do \
	  docker pull $(REGISTRY)/$$img:latest && \
	    docker tag $(REGISTRY)/$$img:latest $$img:latest || true; \
	done
	$(call log_ok,docker,Dev images pulled from $(REGISTRY))

# ------------------------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------------------------

docker-clean:
	$(call log,docker,Cleaning dangling images and stopped containers)
	@docker image prune -f
	@docker container prune -f
	@docker network prune -f

docker-clean-deep:
	$(call log,docker,Deep cleanup including unused images and volumes)
	@docker system prune -af --volumes

docker-prune:
	$(call log,docker,Removing apex project images)
	@docker images --format '{{.Repository}}:{{.Tag}}' | grep '^apex\.' | xargs -r docker rmi -f 2>/dev/null || true
	@docker image prune -f

docker-disk-usage:
	@echo "Docker System Disk Usage:"
	@docker system df
	@echo ""
	@echo "Apex Image Sizes:"
	@docker images --format "  {{.Repository}}:{{.Tag}} => {{.Size}}" | grep apex | sort

# ------------------------------------------------------------------------------
# Validation
# ------------------------------------------------------------------------------

docker-lint:
	$(call log,docker,Running hadolint on all Dockerfiles)
	@find docker -name "*.Dockerfile" -exec hadolint {} \;

docker-validate: docker-lint
	$(call log,docker,Validating docker compose configuration)
	@docker compose config --quiet

# ------------------------------------------------------------------------------
# Release path drift guard
# ------------------------------------------------------------------------------
# final.Dockerfile COPYs each platform from build/<preset-dir>; those names must
# track CMakePresets.json, and every BUILDER_TARGETS entry must have a COPY line
# collecting it. When a preset was renamed and final was not, the release
# silently broke -- caught only by actually running it; a target added to the
# list but not to final.Dockerfile would ship a release missing that platform.
# This static check fails fast on both drift classes, in the gate, not on a
# release tag.
check-release-paths:
	@final_dirs=$$(grep -E 'COPY --from' docker/final.Dockerfile | grep -oE 'build/[A-Za-z0-9._-]+' | sed 's#build/##' | sort -u); \
	preset_dirs=$$(grep -oE 'build/[A-Za-z0-9._-]+' CMakePresets.json | sed 's#build/##' | sort -u); \
	missing=""; \
	for d in $$final_dirs; do printf '%s\n' "$$preset_dirs" | grep -qx "$$d" || missing="$$missing $$d"; done; \
	if [ -n "$$missing" ]; then \
	  printf '[check-release-paths] final.Dockerfile build dirs with no matching CMakePresets binaryDir:%s\n' "$$missing" >&2; \
	  printf 'Known preset dirs:\n%s\n' "$$preset_dirs" >&2; \
	  exit 1; \
	fi; \
	uncollected=""; \
	for t in $(BUILDER_TARGETS); do \
	  grep -q "COPY --from=apex.builder.$$t:" docker/final.Dockerfile || uncollected="$$uncollected $$t"; \
	done; \
	if [ -n "$$uncollected" ]; then \
	  printf '[check-release-paths] BUILDER_TARGETS with no COPY --from in final.Dockerfile:%s\n' "$$uncollected" >&2; \
	  exit 1; \
	fi; \
	printf '[check-release-paths] OK -- final.Dockerfile covers all builder targets with preset-valid paths\n'

# Emit BUILDER_TARGETS as a JSON array for the release.yml builders matrix
# (same pattern as mk/checks.mk print-nightly-checks for the nightly matrix).
print-builder-targets:
	@printf '%s\n' '$(call _json_array,$(BUILDER_TARGETS))'

# Every ghcr package this repo publishes, one per line -- drives the nightly
# ghcr-gc job, so a new dev service or builder target is collected without a
# workflow edit.
print-ghcr-packages:
	@printf '%s\n' $(_DEV_IMAGES) $(patsubst %,apex.builder.%,$(BUILDER_TARGETS))

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: docker-all docker-devs docker-builders docker-base docker-final artifacts
.PHONY: docker-push-devs docker-pull-devs check-release-paths print-builder-targets
.PHONY: print-ghcr-packages
.PHONY: docker-clean docker-clean-deep docker-prune docker-disk-usage
.PHONY: docker-lint docker-validate

endif  # DOCKER_MK_GUARD
