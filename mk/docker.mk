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

# Artifact output directory
DOCKER_OUT_DIR := output

# Project version (extracted from CMakeLists.txt for tarball naming).
# Override at the command line: make artifacts VERSION=1.2.3
VERSION ?= $(shell sed -n 's/.*VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt | head -1)

# Registry for dev image caching (ghcr.io/<owner>/<repo>).
# Set in CI via environment; not required for local builds.
REGISTRY ?=

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

# _builder_target: Generate docker-builder-<name> build target
# $(1) = builder suffix, $(2) = dev service prerequisite
define _builder_target
.PHONY: docker-builder-$(1)
docker-builder-$(1): docker-$(2)
	$$(call _docker_build,builder-$(1))
endef

# ------------------------------------------------------------------------------
# Aggregate Targets
# ------------------------------------------------------------------------------

docker-all: docker-base docker-devs docker-builders docker-final
	$(call log,docker,All images built)

docker-devs: docker-dev docker-dev-cuda docker-dev-jetson \
             docker-dev-rpi docker-dev-riscv64 docker-dev-zephyr \
             docker-dev-stm32 docker-dev-esp32 docker-dev-pico \
             docker-dev-arduino docker-dev-atmega328pb docker-dev-pic32 \
             docker-dev-c2000
	$(call log,docker,All dev images built)

docker-builders: docker-builder-cpu docker-builder-cuda docker-builder-jetson \
                 docker-builder-rpi docker-builder-riscv64 \
                 docker-builder-stm32 docker-builder-arduino \
                 docker-builder-pico docker-builder-esp32 docker-builder-c2000
	$(call log,docker,All builder images built)

# ------------------------------------------------------------------------------
# Base Image
# ------------------------------------------------------------------------------

docker-base:
	$(call _docker_build,base)

# ------------------------------------------------------------------------------
# Development Shell Images (generated from templates)
# ------------------------------------------------------------------------------
# Dependency structure:
#   base -> dev, dev-cuda
#   dev-cuda -> dev-jetson
#   dev (CPU) -> all other dev images

$(eval $(call _dev_target,dev,docker-base))
$(eval $(call _dev_target,dev-cuda,docker-base))
$(eval $(call _dev_target,dev-jetson,docker-dev-cuda))

$(foreach svc,dev-rpi dev-riscv64 dev-zephyr dev-stm32 dev-esp32 dev-pico \
              dev-arduino dev-atmega328pb dev-pic32 dev-c2000,\
  $(eval $(call _dev_target,$(svc),docker-dev)))

# ------------------------------------------------------------------------------
# Builder Images (generated from templates)
# ------------------------------------------------------------------------------

$(eval $(call _builder_target,cpu,dev))
$(eval $(call _builder_target,cuda,dev-cuda))
$(eval $(call _builder_target,jetson,dev-jetson))
$(eval $(call _builder_target,rpi,dev-rpi))
$(eval $(call _builder_target,riscv64,dev-riscv64))
$(eval $(call _builder_target,stm32,dev-stm32))
$(eval $(call _builder_target,arduino,dev-arduino))
$(eval $(call _builder_target,pico,dev-pico))
$(eval $(call _builder_target,esp32,dev-esp32))
$(eval $(call _builder_target,c2000,dev-c2000))

# ------------------------------------------------------------------------------
# Interactive Shells (generated from templates)
# ------------------------------------------------------------------------------

$(foreach svc,dev dev-cuda dev-jetson dev-rpi dev-riscv64 dev-zephyr \
              dev-stm32 dev-esp32 dev-pico dev-arduino dev-atmega328pb \
              dev-pic32 dev-c2000,\
  $(eval $(call _shell_target,$(svc))))

# ------------------------------------------------------------------------------
# Final Packager
# ------------------------------------------------------------------------------

docker-final: docker-builders
	$(call log,docker,Building final image (VERSION=$(VERSION)))
	@docker compose build --build-arg VERSION=$(VERSION) final

# ------------------------------------------------------------------------------
# Artifact Extraction
# ------------------------------------------------------------------------------

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

_DEV_IMAGES := apex.base \
               apex.dev.cpu apex.dev.cuda apex.dev.jetson \
               apex.dev.rpi apex.dev.riscv64 \
               apex.dev.stm32 apex.dev.esp32 apex.dev.pico \
               apex.dev.arduino apex.dev.c2000

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
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: docker-all docker-devs docker-builders docker-base docker-final artifacts
.PHONY: docker-push-devs docker-pull-devs
.PHONY: docker-clean docker-clean-deep docker-prune docker-disk-usage
.PHONY: docker-lint docker-validate

endif  # DOCKER_MK_GUARD
