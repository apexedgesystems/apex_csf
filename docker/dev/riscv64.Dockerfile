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
# RISC-V Cross Toolchain
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-cross-toolchain.sh /usr/local/bin/setup-cross-toolchain
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    setup-cross-toolchain riscv64

# ==============================================================================
# Multi-arch Apt Sources (shared: native amd64 + riscv64 ports, dpkg arch)
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-cross-apt.sh /usr/local/bin/setup-cross-apt
RUN setup-cross-apt riscv64

# ==============================================================================
# RISC-V Sysroot Libraries + mold cross-linker
# ==============================================================================
COPY --chmod=0755 docker/scripts/setup-sysroot.sh /usr/local/bin/setup-sysroot
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    setup-sysroot riscv64 riscv64-linux-gnu

# ==============================================================================
# RISC-V Sysroot
# ==============================================================================
RUN mkdir -p /opt/sysroots/riscv64

ENV CROSS_COMPILE=riscv64-linux-gnu-
ENV RISCV_SYSROOT=/opt/sysroots/riscv64

# Drop privileges once root-only setup is done; the prompt and validation
# steps below need no root.
USER ${USER}

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

WORKDIR /home/${USER}
