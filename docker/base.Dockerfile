# ==============================================================================
# base.Dockerfile - Tiered base images
#
# build-base : compile + link + test the release artifacts. The 10 release
#              builders and the build-test gate inherit only this.
# dev-base   : build-base + tooling (scanners, formatters, profilers, docs).
#              The dev shells (`apex.base` consumers) inherit this.
#
# The `base` compose service builds the dev-base target, so `apex.base` carries
# the full toolset and every current consumer is unaffected.
#
# Usage:
#   docker compose build base                       # -> apex.base (dev-base)
#   docker build --target build-base -f this .      # -> the lean compile/test tier
# ==============================================================================

# ==============================================================================
# Stage: build-base - compile, link, and test the release artifacts
# ==============================================================================
FROM ubuntu:24.04 AS build-base

# Build-time arguments
ARG USER
ARG HOST_UID
ARG HOST_GID
ARG CMAKE_VERSION=4.2.3
ARG UPX_VERSION=5.1.0

LABEL org.opencontainers.image.title="apex.build-base" \
      org.opencontainers.image.description="Compile/link/test tier for Apex CSF C++ builders" \
      org.opencontainers.image.vendor="Apex"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ------------------------------------------------------------------------------
# Environment Configuration
# ------------------------------------------------------------------------------
# Build-time dependency fetches (pip/poetry from PyPI, cargo from crates.io) are
# the build's only compile-time network dependency. Transient transport flakes
# there -- HTTP/2 framing resets, read timeouts -- have failed release builds, so
# make the fetches resilient: bounded retries, a longer timeout, and plain
# HTTP/1.1 for cargo (sidesteps the HTTP/2 framing-layer resets seen against
# crates.io). pip caching stays off (BuildKit cache mounts handle reuse).
ENV PIP_NO_CACHE_DIR=off \
    PIP_DISABLE_PIP_VERSION_CHECK=on \
    PIP_DEFAULT_TIMEOUT=120 \
    PIP_RETRIES=5 \
    CARGO_NET_RETRY=5 \
    CARGO_HTTP_MULTIPLEXING=false

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

# ------------------------------------------------------------------------------
# System Packages
# ------------------------------------------------------------------------------
# Compile toolchain, the libraries the build links, and the utilities the test
# gate needs at runtime. BuildKit cache mounts speed up rebuilds.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      # Networking and certificates (downloads below)
      wget curl lsb-release gnupg ca-certificates \
      # Version control (FetchContent clones at configure time)
      git \
      # Build systems
      make cmake ninja-build \
      # Compiler acceleration (ccache=object cache, mold=fast linker)
      ccache mold \
      # Python for build scripts and the python tools
      python3 python3-pip python3-venv \
      # Privilege (sudoers entry below; profiling parity in dev-base)
      sudo \
      # Test-gate runtime: socat emulation, vcan setup (ip), capabilities
      socat iproute2 libcap2-bin \
      # Hardware interface libraries (bluetooth/gpio protocol modules link these)
      libbluetooth-dev libgpiod2 \
      # Math/crypto libraries (encryption + linalg link these)
      libssl-dev liblapacke-dev libopenblas-dev \
      # Archive and file utilities
      xz-utils file

# ------------------------------------------------------------------------------
# UPX - Executable Packer
# ------------------------------------------------------------------------------
# Compresses release executables. Distro version is outdated, so install from
# GitHub.
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 -O /tmp/upx.tar.xz \
      "https://github.com/upx/upx/releases/download/v${UPX_VERSION}/upx-${UPX_VERSION}-amd64_linux.tar.xz" && \
    tar -C /tmp -xJf /tmp/upx.tar.xz && \
    mv "/tmp/upx-${UPX_VERSION}-amd64_linux/upx" /usr/local/bin/upx && \
    chmod +x /usr/local/bin/upx && \
    rm -rf /tmp/upx.tar.xz "/tmp/upx-${UPX_VERSION}-amd64_linux"

# ------------------------------------------------------------------------------
# LLVM/Clang apt repository
# ------------------------------------------------------------------------------
# Scoped GPG key (signed-by restricts the key to this repo). Shared by the
# compilers here and the analysis tooling in dev-base.
# Download the key to a file with retries (apt.llvm.org throttles/5xx-flakes on CI
# runners) then dearmor -- a piped one-shot fails the whole layer on any transient
# blip, and --retry-on-http-error avoids dearmoring an error page.
RUN install -d -m 0755 /etc/apt/keyrings && \
    wget --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 \
      --waitretry=15 --timeout=30 -qO /tmp/llvm-snapshot.gpg.key \
      https://apt.llvm.org/llvm-snapshot.gpg.key && \
    gpg --dearmor -o /etc/apt/keyrings/llvm.gpg /tmp/llvm-snapshot.gpg.key && \
    rm -f /tmp/llvm-snapshot.gpg.key && \
    echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] http://apt.llvm.org/$(lsb_release -sc)/ llvm-toolchain-$(lsb_release -sc)-21 main" \
      >> /etc/apt/sources.list.d/llvm.list && \
    echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] http://apt.llvm.org/$(lsb_release -sc)/ llvm-toolchain-$(lsb_release -sc)-20 main" \
      >> /etc/apt/sources.list.d/llvm.list

# Compilers: full Clang 21 stack (primary) plus only the clang-20 driver, which
# CMake selects as the CUDA host compiler when the toolkit is a clang major
# behind (CMakeLists.txt). The rest of the 20 stack -- llvm-20, the sanitizer
# runtimes, and the 20 analysis/format tooling -- has no consumer. Plus the
# linker and the C++ runtime. The 21 analysis/format tooling lives in dev-base.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      clang-21 llvm-21 libclang-rt-21-dev \
      clang-20 \
      lld libc++-dev libc++abi-dev

# Unversioned compiler symlinks default to Clang 21. cc/c++ also point at clang
# so cargo and other tools that default to `cc` have a C/C++ driver -- the lean
# tier ships no gcc, and apex builds with clang regardless.
RUN ln -sf /usr/bin/clang-21  /usr/local/bin/clang && \
    ln -sf /usr/bin/clang++-21 /usr/local/bin/clang++ && \
    ln -sf /usr/bin/clang-21  /usr/local/bin/cc && \
    ln -sf /usr/bin/clang++-21 /usr/local/bin/c++

# ------------------------------------------------------------------------------
# CMake
# ------------------------------------------------------------------------------
# Distro version is too old, so we install from Kitware.
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh" && \
    chmod +x cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --skip-license --prefix=/usr/local && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

# ------------------------------------------------------------------------------
# Rust Toolchain
# ------------------------------------------------------------------------------
# Install Rust via rustup to /opt/rust for system-wide access.
# Using /opt avoids permission issues when COPY --from overlays onto CUDA images.
# Includes: rustc, cargo, clippy (linter), rustfmt (formatter). The coverage
# driver (cargo-llvm-cov) is added in dev-base.
ARG RUST_VERSION=stable
ENV RUSTUP_HOME=/opt/rust/rustup \
    CARGO_HOME=/opt/rust/cargo
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain ${RUST_VERSION} --profile minimal && \
    /opt/rust/cargo/bin/rustup component add clippy rustfmt llvm-tools-preview && \
    chmod -R a+rwX /opt/rust
ENV PATH="/opt/rust/cargo/bin:$PATH"

# ------------------------------------------------------------------------------
# Rust dependency cache (hermetic, offline release builds)
# ------------------------------------------------------------------------------
# Pre-fetch the rust tools' pinned dependencies into the shared cargo registry
# so the release build -- and clean rebuilds -- never reach crates.io. A
# crates.io outage, throttle, or yanked version can no longer fail a release,
# and the fetch is paid once per dependency change instead of every build.
#
# Keyed on Cargo.lock: this layer rebuilds only when a dependency changes (the CI
# image graph also watches tools/rust/Cargo.lock so the cache never goes stale).
# --target restricts the fetch to the host triple the rust tools build for,
# dropping ~330 MB of Windows/wasm crates that never compile here (~124 MB cached
# vs ~454 MB unfiltered).
COPY tools/rust/Cargo.toml tools/rust/Cargo.lock /tmp/rust-fetch/
RUN cargo fetch --locked --target x86_64-unknown-linux-gnu \
      --manifest-path /tmp/rust-fetch/Cargo.toml && \
    rm -rf /tmp/rust-fetch && \
    chmod -R a+rwX /opt/rust/cargo

# ------------------------------------------------------------------------------
# Poetry - Python package manager for the python tools (`make tools-py`)
# ------------------------------------------------------------------------------
RUN --mount=type=cache,target=/root/.cache/pip \
    pip3 install --no-cache-dir --break-system-packages poetry

# ------------------------------------------------------------------------------
# Python dependency wheelhouse (hermetic, offline python-tools build)
# ------------------------------------------------------------------------------
# Download the python tools' locked dependencies as wheels so the release build
# installs them offline -- a PyPI outage/yank can no longer fail the build.
# poetry build itself is already offline (poetry-core is bundled). Keyed on
# poetry.lock; the CI image graph watches it too. The dependency bake below adds
# the FetchContent deps' own python-tool wheels to the same wheelhouse.
COPY tools/py/pyproject.toml tools/py/poetry.lock /tmp/py-fetch/
RUN pip3 install --no-cache-dir --break-system-packages poetry-plugin-export && \
    poetry --directory /tmp/py-fetch export --format requirements.txt \
      --output /tmp/py-fetch/req.txt --without-hashes && \
    pip3 download --no-cache-dir --requirement /tmp/py-fetch/req.txt \
      --dest /opt/apex-pip-wheels && \
    rm -rf /tmp/py-fetch

# ------------------------------------------------------------------------------
# FetchContent source + toolchain cache (hermetic, offline configure and build)
# ------------------------------------------------------------------------------
# Clone the build's FetchContent dependencies (fmt, googletest, vernier, seeker)
# at their pinned tags so `cmake` configures offline -- no GitHub at release
# time; ExternalDependencies.cmake redirects FetchContent to these via
# APEX_DEPS_DIR. A dependency that ships its own tools is baked too: its cargo
# crates land in the shared CARGO_HOME (vernier >= 1.0.3 inherits it) and its
# locked python wheels join the wheelhouse, so dependency tool builds are
# offline as well. Keyed on ExternalDependencies.cmake (the pinned-version
# source of truth); the CI image graph watches it too, so a version bump
# rebuilds and re-bakes.
COPY ExternalDependencies.cmake /tmp/deps/ExternalDependencies.cmake
COPY docker/scripts/bake-external-deps.sh /tmp/deps/bake-external-deps.sh
RUN bash /tmp/deps/bake-external-deps.sh /tmp/deps/ExternalDependencies.cmake \
      /opt/apex-deps /opt/apex-pip-wheels && \
    rm -rf /tmp/deps && \
    chmod -R a+rwX /opt/apex-deps /opt/apex-pip-wheels /opt/rust/cargo
ENV APEX_DEPS_DIR=/opt/apex-deps

# ------------------------------------------------------------------------------
# Offline build enforcement
# ------------------------------------------------------------------------------
# Everything a release build resolves is now baked: apex + dependency cargo
# crates (shared CARGO_HOME), apex + dependency python wheels (the wheelhouse),
# and the FetchContent sources. Enforce it globally -- cargo and pip read these
# natively, so apex's AND every dependency's tool builds run offline with no
# per-project flag plumbing. A missing bake fails loudly here instead of
# silently fetching. dev-base re-opens the network for interactive work.
ENV CARGO_NET_OFFLINE=1 \
    PIP_NO_INDEX=1 \
    PIP_FIND_LINKS=/opt/apex-pip-wheels

# ------------------------------------------------------------------------------
# ptest profiler link libraries
# ------------------------------------------------------------------------------
# The perf tests link tcmalloc/profiler when present (find_library in
# cmake/apex/Testing.cmake), so the link-time libs ship in build-base for parity
# with the dev build. The profiler binaries/backends live in dev-base.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libgoogle-perftools-dev \
      libunwind-dev

# ------------------------------------------------------------------------------
# SuiteSparse (KLU sparse solver) - required by the MNA solver
# ------------------------------------------------------------------------------
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libsuitesparse-dev

# ------------------------------------------------------------------------------
# ngspice (SPICE reference simulator) - linked by the electronics sim backend
# ------------------------------------------------------------------------------
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libngspice0-dev \
      ngspice

# ------------------------------------------------------------------------------
# User Setup
# ------------------------------------------------------------------------------
# Centralized here so every downstream image (builders and dev shells) inherits
# the correct user.
RUN userdel -r ubuntu 2>/dev/null || true && \
    groupdel ubuntu 2>/dev/null || true && \
    groupadd -g ${HOST_GID} ${USER} 2>/dev/null || true && \
    useradd -l -m -u ${HOST_UID} -g ${HOST_GID} -s /bin/bash -p "*" ${USER} 2>/dev/null || true && \
    echo "${USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    chown -R ${HOST_UID}:${HOST_GID} /home/${USER} && \
    usermod -aG dialout "${USER}"

# ------------------------------------------------------------------------------
# Cleanup and Validation
# ------------------------------------------------------------------------------
RUN rm -rf /usr/local/man /tmp/*

RUN echo "Validating build-base image..." && \
    cmake --version && \
    clang --version && \
    clang++ --version && \
    ccache --version && \
    mold --version && \
    upx --version | { head -n1; cat >/dev/null; } && \
    rustc --version && \
    cargo --version && \
    echo "build-base image validation: OK"

# Default to the unprivileged user; downstream images that need root for
# package installs re-assert USER root and switch back themselves.
USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Stage: dev-base - build-base + tooling (scanners, formatters, profilers, docs)
# ==============================================================================
FROM build-base AS dev-base

# Interactive dev resolves dependencies online; the offline guarantee is for the
# release builders (build-base tier), not the dev shell. The baked caches are
# still inherited (PIP_FIND_LINKS stays, so pip prefers the wheelhouse before
# PyPI), and this stage's own pip installs below need the index open.
ENV CARGO_NET_OFFLINE= \
    PIP_NO_INDEX=

ARG USER
ARG HADOLINT_VERSION=v2.14.0
ARG SHFMT_VERSION=v3.12.0
ARG BLACK_VERSION=26.1.0
ARG CMAKELANG_VERSION=0.6.13
ARG PRE_COMMIT_VERSION=4.5.1
ARG GITLEAKS_VERSION=8.30.1
ARG OSV_SCANNER_VERSION=2.3.8
ARG TRIVY_VERSION=0.71.0
ARG SEMGREP_VERSION=1.165.0
# cargo-llvm-cov: prebuilt binary (taiki-e) driving `make coverage-rust`.
# Pinned; llvm-tools-preview (from build-base) supplies the profdata tooling.
ARG CARGO_LLVM_COV_VERSION=0.8.7

LABEL org.opencontainers.image.title="apex.base" \
      org.opencontainers.image.description="Base tooling layer for Apex CSF C++ development" \
      org.opencontainers.image.vendor="Apex"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

USER root

# ------------------------------------------------------------------------------
# Dev System Packages
# ------------------------------------------------------------------------------
# Editor, docs generation, shell access, and manual CAN tooling.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      vim \
      doxygen graphviz \
      ssh openssh-client \
      can-utils

# ------------------------------------------------------------------------------
# LLVM/Clang analysis and format tooling
# ------------------------------------------------------------------------------
# clang-21 tidy/format/tools, the LLVM debugger, coverage, and
# include-what-you-use. The compilers themselves live in build-base.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      clang-tidy-21 clang-tools-21 clang-format-21 \
      lldb lcov iwyu gdb

# Unversioned tooling symlinks default to Clang 21
RUN ln -sf /usr/bin/clang-format-21 /usr/local/bin/clang-format && \
    ln -sf /usr/bin/clang-tidy-21   /usr/local/bin/clang-tidy

# ------------------------------------------------------------------------------
# Bloaty - Binary Size Analysis
# ------------------------------------------------------------------------------
# Built from source: not packaged for Ubuntu 24.04. Tracks master tip via
# shallow clone. CMAKE_POLICY_VERSION_MINIMUM=3.5 satisfies upstream submodule
# policy floors.
RUN git clone --recursive --depth 1 https://github.com/google/bloaty.git /tmp/bloaty && \
    CC=clang CXX=clang++ CFLAGS=-Wno-deprecated-non-prototype \
    cmake -S /tmp/bloaty -B /tmp/bloaty/build -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
    cmake --build /tmp/bloaty/build -j && \
    cmake --install /tmp/bloaty/build && \
    rm -rf /tmp/bloaty

# ------------------------------------------------------------------------------
# Linters and Formatters
# ------------------------------------------------------------------------------
# Static binaries for speed and reproducibility.

# hadolint: Dockerfile linter
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 -O /usr/local/bin/hadolint \
      "https://github.com/hadolint/hadolint/releases/download/${HADOLINT_VERSION}/hadolint-linux-x86_64" && \
    chmod +x /usr/local/bin/hadolint

# shfmt: Shell script formatter (note: filename keeps 'v' prefix)
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 -O /usr/local/bin/shfmt \
      "https://github.com/mvdan/sh/releases/download/${SHFMT_VERSION}/shfmt_${SHFMT_VERSION}_linux_amd64" && \
    chmod +x /usr/local/bin/shfmt

# ------------------------------------------------------------------------------
# Python Formatters and Hooks
# ------------------------------------------------------------------------------
# pre-commit: Git hook framework
# black: Python formatter
# cmakelang: CMake formatter (cmake-format, cmake-lint)
RUN --mount=type=cache,target=/root/.cache/pip \
    pip3 install --no-cache-dir --break-system-packages \
      pre-commit==${PRE_COMMIT_VERSION} \
      black==${BLACK_VERSION} \
      cmakelang==${CMAKELANG_VERSION}

# ------------------------------------------------------------------------------
# ninjatracing - Convert .ninja_log to chrome-tracing JSON
# ------------------------------------------------------------------------------
# Single Python script from nico/ninjatracing — not packaged on PyPI.
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 -O /usr/local/bin/ninjatracing \
      https://raw.githubusercontent.com/nico/ninjatracing/main/ninjatracing && \
    chmod +x /usr/local/bin/ninjatracing

# ------------------------------------------------------------------------------
# Static Analysis and Security
# ------------------------------------------------------------------------------
# A second static-analysis engine, a bounded model checker, and supply-chain /
# secret / SAST scanners. Each is exposed as a `make` check target (mk/checks.mk)
# and runs in this container, so local and CI runs are identical.
#
#   cppcheck    : independent static analyzer (+ bundled MISRA C++ addon)
#   cbmc        : bounded model checker -- proves absence of overflow/OOB/assert
#   trivy       : vulnerability + secret + SBOM (CycloneDX/SPDX) scanner
#   gitleaks    : secret scanner
#   osv-scanner : dependency vulnerability scanner (OSV database)
#   semgrep     : pattern-based SAST
# (mull mutation testing is deferred: no LLVM-21 release build yet.)

# cppcheck + cbmc from the distro
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      cppcheck \
      cbmc

# trivy (pinned) via the upstream install script
RUN curl -sfL --retry 5 --retry-connrefused --retry-all-errors --connect-timeout 30 \
      https://raw.githubusercontent.com/aquasecurity/trivy/main/contrib/install.sh | \
      sh -s -- -b /usr/local/bin "v${TRIVY_VERSION}"

# gitleaks: static binary from GitHub releases
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 -O /tmp/gitleaks.tar.gz \
      "https://github.com/gitleaks/gitleaks/releases/download/v${GITLEAKS_VERSION}/gitleaks_${GITLEAKS_VERSION}_linux_x64.tar.gz" && \
    tar -C /usr/local/bin -xzf /tmp/gitleaks.tar.gz gitleaks && \
    chmod +x /usr/local/bin/gitleaks && \
    rm /tmp/gitleaks.tar.gz

# osv-scanner: static binary from GitHub releases
RUN wget --progress=dot:giga --tries=5 --retry-connrefused --retry-on-http-error=429,500,502,503,504 --waitretry=15 --timeout=30 -O /usr/local/bin/osv-scanner \
      "https://github.com/google/osv-scanner/releases/download/v${OSV_SCANNER_VERSION}/osv-scanner_linux_amd64" && \
    chmod +x /usr/local/bin/osv-scanner

# semgrep: pattern SAST (pip, matching the Python-tools convention above)
RUN --mount=type=cache,target=/root/.cache/pip \
    pip3 install --no-cache-dir --break-system-packages \
      semgrep==${SEMGREP_VERSION}

# ------------------------------------------------------------------------------
# cargo-llvm-cov - Rust coverage driver (`make coverage-rust`)
# ------------------------------------------------------------------------------
# chmod only the new binary: build-base already opened /opt/rust tree-wide, and a
# repeat -R here would copy the whole toolchain into this layer.
RUN curl --proto '=https' --tlsv1.2 -fsSL --retry 5 --retry-connrefused --retry-all-errors --connect-timeout 30 \
      "https://github.com/taiki-e/cargo-llvm-cov/releases/download/v${CARGO_LLVM_COV_VERSION}/cargo-llvm-cov-x86_64-unknown-linux-gnu.tar.gz" \
      | tar xzf - -C /opt/rust/cargo/bin && \
    chmod a+rwX /opt/rust/cargo/bin/cargo-llvm-cov

# ------------------------------------------------------------------------------
# FlameGraph
# ------------------------------------------------------------------------------
# Brendan Gregg's scripts for visualizing profiler output as SVG flame graphs.
RUN git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /opt/FlameGraph && \
    ln -s /opt/FlameGraph/flamegraph.pl /usr/local/bin/flamegraph.pl && \
    ln -s /opt/FlameGraph/stackcollapse-perf.pl /usr/local/bin/stackcollapse-perf.pl && \
    ln -s /opt/FlameGraph/difffolded.pl /usr/local/bin/difffolded.pl && \
    chmod +x /opt/FlameGraph/*.pl

ENV FLAMEGRAPH_DIR=/opt/FlameGraph

# ------------------------------------------------------------------------------
# Profiling Tools
# ------------------------------------------------------------------------------
# linux-tools:        perf (common wrapper + generic; host-matched added below)
# google-perftools:   pprof CPU/heap profiler driver (tcmalloc lib is in build-base)
# valgrind:           callgrind, massif, memcheck (vernier backends)
# bpftrace:           dynamic tracing + off-CPU (vernier offcpu backend)
# heaptrack:          low-overhead heap profiler (vernier heaptrack backend)
# libjemalloc2/-dev:  sampling heap profiler (vernier jemalloc backend, jeprof)
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      linux-tools-common \
      linux-tools-generic \
      google-perftools \
      valgrind \
      bpftrace \
      heaptrack \
      libjemalloc2 \
      libjemalloc-dev \
      rr

# Host-kernel-matched perf. linux-tools-generic tracks the latest kernel, which
# drifts ahead of the host's RUNNING kernel; perf needs the exact match. Install
# linux-tools-${HOST_KERNEL} (uname -r from the makefile) so `--profile perf`
# works out of the box. Tolerant: warns rather than fails if that version isn't
# in the archive (e.g. building on a host whose kernel isn't published).
#
# ARG declared here (not at the top) so a host-kernel bump only invalidates this
# layer + the cheap tail, not the whole dev-base image.
ARG HOST_KERNEL
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    if [ -n "${HOST_KERNEL}" ]; then \
      apt-get update && \
      DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
        "linux-tools-${HOST_KERNEL}" \
      || echo "WARN: linux-tools-${HOST_KERNEL} unavailable; perf may not match the host kernel"; \
    else \
      echo "WARN: HOST_KERNEL build-arg empty; perf may not match the host kernel"; \
    fi

# ------------------------------------------------------------------------------
# Cleanup and Validation
# ------------------------------------------------------------------------------
RUN rm -rf /usr/local/man /tmp/*

RUN echo "Validating dev-base image..." && \
    clang-tidy --version && \
    clang-format --version && \
    hadolint --version && \
    shfmt --version && \
    semgrep --version && \
    echo "dev-base image validation: OK"

# Default to the unprivileged user; downstream images that need root for
# package installs re-assert USER root and switch back themselves.
USER ${USER}
WORKDIR /home/${USER}
