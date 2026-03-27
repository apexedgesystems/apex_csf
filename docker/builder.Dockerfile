# ==============================================================================
# builder.Dockerfile - Unified release artifact builder
#
# Parameterized by DEV_IMAGE and BUILD_CMD build args.
#
# Usage:
#   docker compose build builder-cpu
#   docker compose build builder-cuda
#   docker compose build builder-jetson
#   docker compose build builder-rpi
#   docker compose build builder-riscv64
#   docker compose build builder-stm32
# ==============================================================================
ARG DEV_IMAGE=apex.dev.cpu

FROM ${DEV_IMAGE}:latest

ARG USER
ARG HOST_UID
ARG HOST_GID
ARG BUILD_CMD="make release"

LABEL org.opencontainers.image.title="apex.builder" \
      org.opencontainers.image.description="Release artifact builder"

ENV CONTAINER=yes

USER ${USER}
WORKDIR /home/${USER}/workspace

# ==============================================================================
# Source Code
# ==============================================================================
COPY --chown=${HOST_UID}:${HOST_GID} . .

# ==============================================================================
# Build Release Artifacts
# ==============================================================================
RUN make distclean 2>/dev/null || true && \
    eval ${BUILD_CMD}
