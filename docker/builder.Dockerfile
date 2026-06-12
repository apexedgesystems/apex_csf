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

FROM ${DEV_IMAGE}:latest AS build

ARG USER
ARG HOST_UID
ARG HOST_GID
ARG BUILD_CMD="make release"

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
# Cache mount on /ccache (CCACHE_DIR set in parent dev image). Cache key
# defaults to target path, so all builder-* images share the cache.
#
# Grouping matters: && and || bind with equal precedence left-to-right, so an
# ungrouped trailing `|| true` (for the best-effort ccache stats) would also
# swallow a BUILD_CMD failure -- builders then push partial build trees as
# green. Only distclean and the stats line are best-effort; the build itself
# must fail the image build.
RUN --mount=type=cache,target=/ccache,uid=${HOST_UID},gid=${HOST_GID} \
    { make distclean 2>/dev/null || true; } && \
    eval "${BUILD_CMD}" && \
    { ccache -s 2>/dev/null || true; }

# ==============================================================================
# Export Stage -- build tree only
# ==============================================================================
# The only consumer of a builder image is final.Dockerfile's
# COPY --from=apex.builder.<t>:latest /home/<user>/workspace/build/<dir>, so
# the shipped image carries just the build tree at that same path. The
# SDK-heavy dev toolchain and source stay in the discarded build stage:
# pushing/pulling 10 builders moves gigabytes less through ghcr per release.
FROM scratch

ARG USER

LABEL org.opencontainers.image.title="apex.builder" \
      org.opencontainers.image.description="Release artifact builder (build tree export)"

COPY --from=build /home/${USER}/workspace/build /home/${USER}/workspace/build
