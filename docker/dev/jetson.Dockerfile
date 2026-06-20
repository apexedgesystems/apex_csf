# ==============================================================================
# dev/jetson.Dockerfile - CUDA + AArch64 cross-compilation shell
#
# Includes aarch64 toolchain directly (no separate toolchain image).
#
# Usage:
#   make shell-dev-jetson
#   docker compose run dev-jetson
# ==============================================================================
# BASE selects the CUDA tier: apex.dev.cuda (full toolkit + Nsight, for the dev
# shell) or apex.cuda-build (lean, for the release builder). Both carry the
# cuda-keyring + CUDA components the aarch64 cross install below needs.
ARG BASE=apex.dev.cuda
FROM ${BASE}:latest

ARG USER

LABEL org.opencontainers.image.description="CUDA + AArch64 cross-compilation for Jetson"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# AArch64 Cross-compilation Toolchain
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-cross-toolchain.sh /usr/local/bin/setup-cross-toolchain
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    setup-cross-toolchain arm64

ENV AARCH64_SYSROOT=/usr/aarch64-linux-gnu

# ==============================================================================
# Multi-arch Apt Sources (shared: native amd64 + arm64 ports, dpkg arch)
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-cross-apt.sh /usr/local/bin/setup-cross-apt
RUN setup-cross-apt arm64

# ==============================================================================
# ARM64 Sysroot Libraries + mold cross-linker
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-sysroot.sh /usr/local/bin/setup-sysroot
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    setup-sysroot arm64 aarch64-linux-gnu

# ==============================================================================
# CUDA Cross-compilation (sbsa)
# ==============================================================================
# cuda-keyring is already installed by the parent apex.dev.cuda image.
# Just add the cross-compilation sbsa repo and install cross packages.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    set -euo pipefail && \
    echo "deb [signed-by=/usr/share/keyrings/cuda-archive-keyring.gpg] https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/cross-linux-sbsa/ /" \
      > /etc/apt/sources.list.d/cuda-cross.list && \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y cuda-cross-sbsa || true && \
    CUDA_MM="$(nvcc --version | sed -n 's/^.*release \([0-9]\+\)\.\([0-9]\+\).*/\1-\2/p')" && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y "cuda-cross-sbsa-${CUDA_MM}" || true && \
    PKG="$(apt-cache search '^cuda-cross-sbsa-[0-9]\+-[0-9]\+$' | awk '{print $1}' | sort -V | tail -1 || true)" && \
    { [ -z "${PKG}" ] || DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y "${PKG}"; }

# ==============================================================================
# CUDA AArch64 Target Symlink
# ==============================================================================
RUN set -euo pipefail && \
    if [ ! -f /usr/local/cuda/targets/aarch64-linux/include/cuda_runtime.h ]; then \
      shopt -s nullglob; cand=( \
        /usr/local/cuda/targets/aarch64-linux \
        /usr/local/cuda/targets/sbsa-linux \
        /usr/local/cuda-*/targets/aarch64-linux \
        /usr/local/cuda-*/targets/sbsa-linux ); \
      if [ "${#cand[@]}" -gt 0 ]; then \
        target="${cand[-1]}"; mkdir -p /usr/local/cuda/targets; ln -sfn "${target}" /usr/local/cuda/targets/aarch64-linux; \
      fi; \
    fi && \
    test -f /usr/local/cuda/targets/aarch64-linux/include/cuda_runtime.h \
    || { find /usr/local -maxdepth 3 -type d -path '*/targets/*' -print || true; exit 1; } && \
    (command -v ldconfig >/dev/null 2>&1 && ldconfig || true)

# Cross-compilation pkg-config paths
ENV PKG_CONFIG_LIBDIR=${AARCH64_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${AARCH64_SYSROOT}/usr/lib/pkgconfig:${AARCH64_SYSROOT}/usr/share/pkgconfig
ENV PKG_CONFIG_SYSROOT_DIR=${AARCH64_SYSROOT}

# Drop privileges once root-only setup is done; the prompt and validation
# steps below need no root.
USER ${USER}

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;36m\][JETSON] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN aarch64-linux-gnu-gcc --version && \
    nvcc --version && \
    echo "Jetson image validation: OK"

WORKDIR /home/${USER}
