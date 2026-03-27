# ==============================================================================
# dev/cuda.Dockerfile - CUDA shell with Nsight profiling tools
#
# Installs CUDA toolkit from NVIDIA apt repos directly into apex.base.
# No more COPY --from overlay, so ENV, symlinks, and user accounts are preserved.
#
# Usage:
#   make shell-dev-cuda
#   docker compose run dev-cuda
# ==============================================================================
FROM apex.base:latest

ARG USER
ARG CUDA_VERSION_MAJOR=13
ARG CUDA_VERSION_MINOR=1

LABEL org.opencontainers.image.title="apex.dev.cuda" \
      org.opencontainers.image.description="CUDA shell for Apex CSF"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# NVIDIA CUDA Toolkit (replaces nvidia/cuda base image)
# ==============================================================================
# Install cuda-keyring for apt authentication, then the full toolkit.
# This is exactly what the nvidia/cuda Docker image does internally.
RUN wget -O /tmp/cuda-keyring.deb \
      https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb && \
    dpkg -i /tmp/cuda-keyring.deb && \
    rm /tmp/cuda-keyring.deb

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      cuda-toolkit-${CUDA_VERSION_MAJOR}-${CUDA_VERSION_MINOR}

# CUDA environment (mirrors nvidia/cuda image ENV)
ENV PATH="/usr/local/cuda/bin:${PATH}"
ENV LD_LIBRARY_PATH="/usr/local/cuda/lib64:${LD_LIBRARY_PATH}"
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
# Nsight Systems (Nsight Compute already included via cuda-toolkit)
# ==============================================================================
# nsight-systems-cli comes from the devtools repo (not in cuda-toolkit).
# nsight-compute (ncu) is already installed as cuda-nsight-compute-<ver>.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    wget -qO - https://developer.download.nvidia.com/devtools/repos/ubuntu2404/amd64/nvidia.pub | \
      gpg --dearmor -o /usr/share/keyrings/nvidia-devtools.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/nvidia-devtools.gpg] https://developer.download.nvidia.com/devtools/repos/ubuntu2404/amd64/ /" \
      > /etc/apt/sources.list.d/nvidia-devtools.list && \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      nsight-systems-cli

RUN nsys --version 2>/dev/null || echo "nsys installed (requires GPU at runtime)" && \
    ncu --version 2>/dev/null || echo "ncu installed (requires GPU at runtime)"

# ==============================================================================
# NVML Stub
# ==============================================================================
# Stub allows builds without a GPU; real driver is mounted at runtime.
RUN ln -sf /usr/local/cuda/targets/x86_64-linux/lib/stubs/libnvidia-ml.so \
           /usr/local/cuda/targets/x86_64-linux/lib/libnvidia-ml.so && \
    ldconfig

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

USER ${USER}
WORKDIR /home/${USER}
