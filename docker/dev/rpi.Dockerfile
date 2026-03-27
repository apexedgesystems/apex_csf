# ==============================================================================
# dev/rpi.Dockerfile - Raspberry Pi cross-compilation shell
#
# Includes aarch64 toolchain + RPi sysroot directly (no separate toolchain image).
#
# Usage:
#   make shell-dev-rpi
#   docker compose run dev-rpi
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER

LABEL org.opencontainers.image.title="apex.dev.rpi" \
      org.opencontainers.image.description="Raspberry Pi cross-compilation shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# AArch64 Cross-compilation Toolchain (was toolchain/aarch64 + rpi)
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
ENV PKG_CONFIG_LIBDIR=${AARCH64_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${AARCH64_SYSROOT}/usr/lib/pkgconfig:${AARCH64_SYSROOT}/usr/share/pkgconfig
ENV PKG_CONFIG_SYSROOT_DIR=${AARCH64_SYSROOT}

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
# Raspberry Pi Sysroot and Tools
# ==============================================================================
RUN mkdir -p /opt/sysroots/rpi

ENV RPI_SYSROOT=/opt/sysroots/rpi

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      kpartx \
      parted \
      dosfstools \
      rsync \
      sshpass

# ==============================================================================
# CI Profiling Support
# ==============================================================================
RUN if [ -n "${USER}" ]; then \
      echo "${USER} ALL=(ALL) NOPASSWD: /usr/bin/perf, /usr/bin/bpftrace" > /etc/sudoers.d/profilers && \
      chmod 0440 /etc/sudoers.d/profilers; \
    fi

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;35m\][RPI] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN aarch64-linux-gnu-gcc --version && \
    echo "Raspberry Pi image validation: OK"

USER ${USER}
WORKDIR /home/${USER}
