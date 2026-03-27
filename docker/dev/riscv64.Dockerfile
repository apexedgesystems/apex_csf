# ==============================================================================
# dev/riscv64.Dockerfile - RISC-V 64-bit cross-compilation shell
#
# Usage:
#   make shell-dev-riscv64
#   docker compose run dev-riscv64
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER

LABEL org.opencontainers.image.title="apex.dev.riscv64" \
      org.opencontainers.image.description="RISC-V 64-bit cross-compilation shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# RISC-V Cross Toolchain (was toolchain/riscv64.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      gcc-riscv64-linux-gnu \
      g++-riscv64-linux-gnu \
      binutils-riscv64-linux-gnu \
      qemu-user-static \
      file

# ==============================================================================
# Multi-arch Apt Sources (riscv64 packages live on ports.ubuntu.com)
# ==============================================================================
RUN rm -f /etc/apt/sources.list.d/ubuntu.sources && \
    printf '%s\n' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main restricted universe multiverse' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main restricted universe multiverse' \
      'deb [arch=amd64] http://security.ubuntu.com/ubuntu noble-security main restricted universe multiverse' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-backports main restricted universe multiverse' \
      > /etc/apt/sources.list && \
    printf '%s\n' \
      'deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse' \
      'deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse' \
      'deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse' \
      'deb [arch=riscv64] http://ports.ubuntu.com/ubuntu-ports noble-backports main restricted universe multiverse' \
      > /etc/apt/sources.list.d/ubuntu-riscv64-ports.list

# ==============================================================================
# RISC-V Sysroot Libraries
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    dpkg --add-architecture riscv64 && \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libgoogle-perftools-dev:riscv64 \
      libtcmalloc-minimal4t64:riscv64 \
      libunwind-dev:riscv64 \
      libssl-dev:riscv64 \
      liblapacke-dev:riscv64 \
      libopenblas-dev:riscv64 \
      zlib1g-dev:riscv64

# ==============================================================================
# RISC-V Sysroot
# ==============================================================================
RUN mkdir -p /opt/sysroots/riscv64

ENV CROSS_COMPILE=riscv64-linux-gnu-
ENV RISCV_SYSROOT=/opt/sysroots/riscv64

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;37m\][RISCV] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN riscv64-linux-gnu-gcc --version && \
    qemu-riscv64-static --version && \
    echo "RISC-V image validation: OK"

USER ${USER}
WORKDIR /home/${USER}
