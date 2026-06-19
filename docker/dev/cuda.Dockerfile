# ==============================================================================
# dev/cuda.Dockerfile - CUDA layer, tiered by BASE
#
# Installs the CUDA toolkit from NVIDIA apt repos onto the selected base:
#   BASE=apex.base       -> apex.dev.cuda   (dev-base + CUDA + Nsight)
#   BASE=apex.build-base -> apex.cuda-build (build-base + CUDA, no Nsight)
# Nsight (INSTALL_NSIGHT=1) is the dev/profiling tier only; the lean cuda-build
# tier the CI gate and builders use sets INSTALL_NSIGHT=0.
#
# Usage:
#   make shell-dev-cuda
#   docker compose run dev-cuda
# ==============================================================================
ARG BASE=apex.base
FROM ${BASE}:latest

ARG USER
# The toolkit's CUDA minor must not exceed what the host driver provides.
# Apps built a minor ahead of the driver still run via forward-compat, but
# nsys cannot trace them -- its CUDA activity capture silently yields nothing
# (ncu and in-process CUPTI are unaffected). Pin to the lowest CUDA minor the
# target fleet's drivers report from nvidia-smi.
ARG CUDA_VERSION_MAJOR=13
ARG CUDA_VERSION_MINOR=0

# Tier selector: 1 = dev (full cuda-toolkit metapackage + Nsight, for the dev
# shell and the optimization workflow); 0 = lean cuda-build (CUDA build
# components only -- nvcc + the libraries the .cu surface links -- dropping
# Nsight, cuda-gdb, the JRE/GTK GUI stack, docs, and the sanitizer).
ARG INSTALL_NSIGHT=1

LABEL org.opencontainers.image.description="CUDA layer for Apex CSF"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# base now ends as the unprivileged user; re-assert root for the toolkit install.
USER root

# ==============================================================================
# NVIDIA CUDA Toolkit (replaces nvidia/cuda base image)
# ==============================================================================
# Install cuda-keyring for apt authentication, then the full toolkit.
# This is exactly what the nvidia/cuda Docker image does internally.
RUN wget -qO /tmp/cuda-keyring.deb \
      https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb && \
    dpkg -i /tmp/cuda-keyring.deb && \
    rm /tmp/cuda-keyring.deb

# Dev tier installs the full toolkit metapackage (Nsight, cuda-gdb, samples,
# docs, JRE/GTK -- ~7 GB). The lean cuda-build tier installs only the build
# components the apex .cu surface compiles and links against: nvcc, the runtime
# + driver headers, NVTX/CUPTI/NVML, and the math libraries. cuDSS is added
# from its archive below; toolkit-config-common provides the /usr/local/cuda
# symlink the ENV and stub steps rely on.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    v="${CUDA_VERSION_MAJOR}-${CUDA_VERSION_MINOR}" && \
    apt-get update && \
    if [ "${INSTALL_NSIGHT}" = "1" ]; then \
      DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
        "cuda-toolkit-${v}"; \
    else \
      DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
        "cuda-nvcc-${v}" "cuda-cudart-dev-${v}" "cuda-nvrtc-dev-${v}" \
        "cuda-driver-dev-${v}" "cuda-cupti-dev-${v}" "cuda-nvml-dev-${v}" \
        "cuda-nvtx-${v}" "cuda-toolkit-${v}-config-common" \
        "libcublas-dev-${v}" "libcusolver-dev-${v}" "libcufft-dev-${v}" \
        "libcusparse-dev-${v}"; \
    fi

# CUDA environment (mirrors nvidia/cuda image ENV). The base sets no
# LD_LIBRARY_PATH, so set it outright rather than self-appending an empty value
# (which would leave a trailing colon -- i.e. CWD -- on the search path).
ENV PATH="/usr/local/cuda/bin:${PATH}"
ENV LD_LIBRARY_PATH="/usr/local/cuda/lib64"
ENV LIBRARY_PATH="/usr/local/cuda/lib64/stubs"
ENV CUDA_VERSION=${CUDA_VERSION_MAJOR}.${CUDA_VERSION_MINOR}

# NVIDIA Container Toolkit runtime requirements.
# Without this, the runtime infers a simple cuda>=X.Y check against the
# reported CUDA capability, which can fail even when the driver is compatible.
# The official nvidia/cuda images set this with a full driver allowlist.
ENV NVIDIA_REQUIRE_CUDA="cuda>=${CUDA_VERSION_MAJOR}.${CUDA_VERSION_MINOR} driver>=535 driver>=550 driver>=570 driver>=575 driver>=580"
ENV NVIDIA_VISIBLE_DEVICES=all
ENV NVIDIA_DRIVER_CAPABILITIES=compute,utility

# ==============================================================================
# Nsight Systems -- dev/profiling tier only
# ==============================================================================
# nsight-systems-cli comes from the devtools repo (not in cuda-toolkit).
# nsight-compute (ncu) ships inside cuda-toolkit. The lean cuda-build tier
# (INSTALL_NSIGHT=0) carries neither nsys nor the devtools repo: the optimization
# workflow that needs them runs in dev-cuda.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    if [ "${INSTALL_NSIGHT}" = "1" ]; then \
      apt-get update && \
      wget -qO - https://developer.download.nvidia.com/devtools/repos/ubuntu2404/amd64/nvidia.pub | \
        gpg --dearmor -o /usr/share/keyrings/nvidia-devtools.gpg && \
      echo "deb [signed-by=/usr/share/keyrings/nvidia-devtools.gpg] https://developer.download.nvidia.com/devtools/repos/ubuntu2404/amd64/ /" \
        > /etc/apt/sources.list.d/nvidia-devtools.list && \
      apt-get update && \
      DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
        nsight-systems-cli && \
      { nsys --version 2>/dev/null || echo "nsys installed (requires GPU at runtime)"; } && \
      { ncu --version 2>/dev/null || echo "ncu installed (requires GPU at runtime)"; } ; \
    else \
      echo "Skipping Nsight (lean cuda-build tier)"; \
    fi

# ==============================================================================
# NVML Stub
# ==============================================================================
# Stub allows builds without a GPU; real driver is mounted at runtime.
RUN ln -sf /usr/local/cuda/targets/x86_64-linux/lib/stubs/libnvidia-ml.so \
           /usr/local/cuda/targets/x86_64-linux/lib/libnvidia-ml.so && \
    ldconfig

# ==============================================================================
# cuDSS (NVIDIA Direct Sparse Solver)
# ==============================================================================
# GPU sparse-LU direct solver used by the MNA cuDSS probes. Not packaged in the
# CUDA apt repo, so install the redistributable archive into the CUDA prefix so
# cudss.h, libcudss.so and the CMake package config are all on the default paths.
ARG CUDSS_VERSION=0.8.0.10
RUN wget -qO /tmp/cudss.tar.xz \
      "https://developer.download.nvidia.com/compute/cudss/redist/libcudss/linux-x86_64/libcudss-linux-x86_64-${CUDSS_VERSION}_cuda${CUDA_VERSION_MAJOR}-archive.tar.xz" && \
    mkdir -p /tmp/cudss && \
    tar -xf /tmp/cudss.tar.xz -C /tmp/cudss --strip-components=1 && \
    cp -a /tmp/cudss/include/. /usr/local/cuda/include/ && \
    cp -a /tmp/cudss/lib/. /usr/local/cuda/lib64/ && \
    ldconfig && \
    rm -rf /tmp/cudss /tmp/cudss.tar.xz

# Drop privileges once root-only setup is done; the prompt and validation
# steps below need no root.
USER ${USER}

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;32m\][CUDA] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN nvcc --version && \
    echo "CUDA image validation: OK"

WORKDIR /home/${USER}
