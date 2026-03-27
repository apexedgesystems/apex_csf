# ==============================================================================
# dev/jetson.Dockerfile - CUDA + AArch64 cross-compilation shell
#
# Includes aarch64 toolchain directly (no separate toolchain image).
#
# Usage:
#   make shell-dev-jetson
#   docker compose run dev-jetson
# ==============================================================================
FROM apex.dev.cuda:latest

ARG USER

LABEL org.opencontainers.image.title="apex.dev.jetson" \
      org.opencontainers.image.description="CUDA + AArch64 cross-compilation shell for Jetson"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# AArch64 Cross-compilation Toolchain (was toolchain/aarch64.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      crossbuild-essential-arm64 \
      binutils-aarch64-linux-gnu \
      pkg-config \
      qemu-user-static \
      file

ENV AARCH64_SYSROOT=/usr/aarch64-linux-gnu

# ==============================================================================
# Multi-arch Apt Sources
# ==============================================================================
RUN rm -f /etc/apt/sources.list.d/ubuntu.sources && \
    printf '%s\n' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main restricted universe multiverse' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main restricted universe multiverse' \
      'deb [arch=amd64] http://security.ubuntu.com/ubuntu noble-security main restricted universe multiverse' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-backports main restricted universe multiverse' \
      > /etc/apt/sources.list && \
    printf '%s\n' \
      'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse' \
      'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse' \
      'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse' \
      'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-backports main restricted universe multiverse' \
      > /etc/apt/sources.list.d/ubuntu-arm64-ports.list

# ==============================================================================
# ARM64 Sysroot Libraries
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    dpkg --add-architecture arm64 && \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libgoogle-perftools-dev:arm64 \
      libtcmalloc-minimal4:arm64 \
      libunwind-dev:arm64 \
      libssl-dev:arm64 \
      zlib1g-dev:arm64 \
      liblapacke-dev:arm64 \
      libopenblas-dev:arm64

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

USER ${USER}
WORKDIR /home/${USER}
