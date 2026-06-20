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
# AArch64 Cross-compilation Toolchain
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-cross-toolchain.sh /usr/local/bin/setup-cross-toolchain
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    setup-cross-toolchain arm64

ENV AARCH64_SYSROOT=/usr/aarch64-linux-gnu
ENV PKG_CONFIG_LIBDIR=${AARCH64_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${AARCH64_SYSROOT}/usr/lib/pkgconfig:${AARCH64_SYSROOT}/usr/share/pkgconfig
ENV PKG_CONFIG_SYSROOT_DIR=${AARCH64_SYSROOT}

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

# Drop privileges once root-only setup is done; the prompt and validation
# steps below need no root.
USER ${USER}

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

WORKDIR /home/${USER}
