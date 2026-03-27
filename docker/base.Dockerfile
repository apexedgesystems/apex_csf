# ==============================================================================
# base.Dockerfile - Shared build tools, compilers, formatters, and profilers
#
# Usage:
#   docker compose build base
# ==============================================================================
FROM ubuntu:24.04

# Build-time arguments
ARG USER
ARG HOST_UID
ARG HOST_GID
ARG CMAKE_VERSION=4.2.3
ARG UPX_VERSION=5.1.0
ARG HADOLINT_VERSION=v2.14.0
ARG SHFMT_VERSION=v3.12.0
ARG BLACK_VERSION=26.1.0
ARG CMAKELANG_VERSION=0.6.13
ARG PRE_COMMIT_VERSION=4.5.1

LABEL org.opencontainers.image.title="apex.base" \
      org.opencontainers.image.description="Base tooling layer for Apex CSF C++ development" \
      org.opencontainers.image.vendor="Apex"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# Environment Configuration
# ==============================================================================
# pip: disable caching (we use BuildKit cache mounts instead)
ENV PIP_NO_CACHE_DIR=off \
    PIP_DISABLE_PIP_VERSION_CHECK=on \
    PIP_DEFAULT_TIMEOUT=100

# Container detection flag for scripts that behave differently in containers
ENV CONTAINER=yes

# BLAS/OpenMP thread safety: prevent thread explosion during parallel builds.
# Without limits, 16 parallel tests on 16 cores = 256 threads per library.
ENV OMP_NUM_THREADS=1 \
    OPENBLAS_NUM_THREADS=1 \
    MKL_NUM_THREADS=1 \
    BLIS_NUM_THREADS=1 \
    VECLIB_MAXIMUM_THREADS=1 \
    OMP_MAX_ACTIVE_LEVELS=1 \
    OMP_NESTED=false

# ccache: mount volume at /ccache to persist across runs
ENV CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=5G \
    CCACHE_COMPRESS=1

# Create ccache directory with sticky bit so any user can write.
# Mount a host volume here to persist cache across container runs.
RUN mkdir -p /ccache && chmod 1777 /ccache

# ==============================================================================
# System Packages
# ==============================================================================
# Core build tools and libraries. BuildKit cache mounts speed up rebuilds.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      # Networking and certificates
      wget curl lsb-release gnupg ca-certificates \
      # Editor and version control
      vim git \
      # Build systems
      make cmake ninja-build \
      # Compiler acceleration (ccache=object cache, mold=fast linker)
      ccache mold \
      # Python for build scripts and formatters
      python3 python3-pip python3-venv \
      # Documentation
      doxygen graphviz \
      # System utilities
      sudo socat iproute2 ssh openssh-client \
      # CAN bus and capabilities (embedded/automotive)
      can-utils libcap2-bin \
      # Hardware interface libraries
      libbluetooth-dev libgpiod2 \
      # Math/crypto libraries
      libssl-dev liblapacke-dev libopenblas-dev \
      # Archive and file utilities
      xz-utils file

# ==============================================================================
# UPX - Executable Packer
# ==============================================================================
# Compresses executables for smaller deployment artifacts.
# Distro version is outdated, so we install from GitHub.
RUN wget --progress=dot:giga -O /tmp/upx.tar.xz \
      "https://github.com/upx/upx/releases/download/v${UPX_VERSION}/upx-${UPX_VERSION}-amd64_linux.tar.xz" && \
    tar -C /tmp -xJf /tmp/upx.tar.xz && \
    mv "/tmp/upx-${UPX_VERSION}-amd64_linux/upx" /usr/local/bin/upx && \
    chmod +x /usr/local/bin/upx && \
    rm -rf /tmp/upx.tar.xz "/tmp/upx-${UPX_VERSION}-amd64_linux"

# ==============================================================================
# LLVM/Clang Toolchain
# ==============================================================================
#   - Clang 21: Latest features (default)
#   - Clang 20: Stable fallback
# Each includes: compiler, analyzer, tidy, format, runtime libs.

# Add LLVM apt repository with scoped GPG key (signed-by restricts key to this repo)
RUN install -d -m 0755 /etc/apt/keyrings && \
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | \
      gpg --dearmor -o /etc/apt/keyrings/llvm.gpg && \
    echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] http://apt.llvm.org/$(lsb_release -sc)/ llvm-toolchain-$(lsb_release -sc)-21 main" \
      >> /etc/apt/sources.list.d/llvm.list && \
    echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] http://apt.llvm.org/$(lsb_release -sc)/ llvm-toolchain-$(lsb_release -sc)-20 main" \
      >> /etc/apt/sources.list.d/llvm.list

# Install Clang versions
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      # Clang 21 (primary)
      clang-21 llvm-21 clang-tidy-21 clang-tools-21 clang-format-21 libclang-rt-21-dev \
      # Clang 20 (stable)
      clang-20 llvm-20 clang-tidy-20 clang-tools-20 clang-format-20 libclang-rt-20-dev \
      # Shared LLVM tools
      lld lldb libc++-dev libc++abi-dev lcov iwyu gdb

# Unversioned symlinks default to Clang 21
RUN ln -sf /usr/bin/clang-format-21 /usr/local/bin/clang-format && \
    ln -sf /usr/bin/clang-tidy-21   /usr/local/bin/clang-tidy && \
    ln -sf /usr/bin/clang-21        /usr/local/bin/clang && \
    ln -sf /usr/bin/clang++-21      /usr/local/bin/clang++

# ==============================================================================
# CMake
# ==============================================================================
# Distro version is too old, so we install from Kitware.
RUN wget --progress=dot:giga \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh" && \
    chmod +x cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --skip-license --prefix=/usr/local && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

# ==============================================================================
# Linters and Formatters
# ==============================================================================
# Static binaries for speed and reproducibility.

# hadolint: Dockerfile linter
RUN wget --progress=dot:giga -O /usr/local/bin/hadolint \
      "https://github.com/hadolint/hadolint/releases/download/${HADOLINT_VERSION}/hadolint-linux-x86_64" && \
    chmod +x /usr/local/bin/hadolint

# shfmt: Shell script formatter (note: filename keeps 'v' prefix)
RUN wget --progress=dot:giga -O /usr/local/bin/shfmt \
      "https://github.com/mvdan/sh/releases/download/${SHFMT_VERSION}/shfmt_${SHFMT_VERSION}_linux_amd64" && \
    chmod +x /usr/local/bin/shfmt

# ==============================================================================
# Python Tools
# ==============================================================================
# pre-commit: Git hook framework
# black: Python formatter
# cmakelang: CMake formatter (cmake-format, cmake-lint)
# poetry: Python package manager
RUN --mount=type=cache,target=/root/.cache/pip \
    pip3 install --no-cache-dir --break-system-packages \
      pre-commit==${PRE_COMMIT_VERSION} \
      black==${BLACK_VERSION} \
      cmakelang==${CMAKELANG_VERSION} \
      poetry

# ==============================================================================
# Rust Toolchain
# ==============================================================================
# Install Rust via rustup to /opt/rust for system-wide access.
# Using /opt avoids permission issues when COPY --from overlays onto CUDA images.
# Includes: rustc, cargo, clippy (linter), rustfmt (formatter)
ARG RUST_VERSION=stable
ENV RUSTUP_HOME=/opt/rust/rustup \
    CARGO_HOME=/opt/rust/cargo
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain ${RUST_VERSION} --profile minimal && \
    /opt/rust/cargo/bin/rustup component add clippy rustfmt && \
    chmod -R a+rwX /opt/rust
ENV PATH="/opt/rust/cargo/bin:$PATH"

# ==============================================================================
# FlameGraph
# ==============================================================================
# Brendan Gregg's scripts for visualizing profiler output as SVG flame graphs.
RUN git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /opt/FlameGraph && \
    ln -s /opt/FlameGraph/flamegraph.pl /usr/local/bin/flamegraph.pl && \
    ln -s /opt/FlameGraph/stackcollapse-perf.pl /usr/local/bin/stackcollapse-perf.pl && \
    ln -s /opt/FlameGraph/difffolded.pl /usr/local/bin/difffolded.pl && \
    chmod +x /opt/FlameGraph/*.pl

ENV FLAMEGRAPH_DIR=/opt/FlameGraph

# ==============================================================================
# Profiling Tools
# ==============================================================================
# linux-tools: perf (may not match host kernel; mount host tools if needed)
# google-perftools: CPU/heap profiler, tcmalloc
# valgrind: callgrind instruction-level profiler
# bpftrace: Dynamic tracing (requires privileged container)
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      linux-tools-common \
      linux-tools-generic \
      google-perftools \
      libgoogle-perftools-dev \
      libunwind-dev \
      valgrind \
      bpftrace

# ==============================================================================
# SuiteSparse (KLU sparse solver)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libsuitesparse-dev

# ==============================================================================
# ngspice (SPICE reference simulator)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libngspice0-dev \
      ngspice

# ==============================================================================
# User Setup
# ==============================================================================
# Centralized here so every downstream image inherits the correct user.
# Dev images no longer need to repeat this block.
RUN userdel -r ubuntu 2>/dev/null || true && \
    groupdel ubuntu 2>/dev/null || true && \
    groupadd -g ${HOST_GID} ${USER} 2>/dev/null || true && \
    useradd -m -u ${HOST_UID} -g ${HOST_GID} -s /bin/bash -p "*" ${USER} 2>/dev/null || true && \
    echo "${USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    chown -R ${HOST_UID}:${HOST_GID} /home/${USER} && \
    usermod -aG dialout "${USER}"

# ==============================================================================
# Cleanup and Validation
# ==============================================================================
RUN rm -rf /usr/local/man /tmp/*

RUN echo "Validating base image..." && \
    cmake --version && \
    clang --version && \
    ccache --version && \
    mold --version && \
    upx --version | head -1 && \
    hadolint --version && \
    shfmt --version && \
    rustc --version && \
    cargo --version && \
    echo "Base image validation: OK"

WORKDIR /home/${USER}