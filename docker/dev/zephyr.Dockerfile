# ==============================================================================
# dev/zephyr.Dockerfile - Zephyr RTOS shell
#
# Usage:
#   make shell-dev-zephyr
#   docker compose run dev-zephyr
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER
ARG ZEPHYR_SDK_VERSION=0.16.8

LABEL org.opencontainers.image.title="apex.dev.zephyr" \
      org.opencontainers.image.description="Zephyr RTOS shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# Zephyr Host Dependencies (was toolchain/zephyr.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      ninja-build \
      gperf \
      device-tree-compiler \
      python3-dev \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      python3-venv \
      picocom \
      xz-utils \
      file \
      libsdl2-dev

# ==============================================================================
# Zephyr SDK Installation
# ==============================================================================
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

RUN set -euo pipefail && \
    ZEPHYR_SDK_URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz" && \
    wget --progress=dot:giga -O /tmp/zephyr-sdk.tar.xz "${ZEPHYR_SDK_URL}" && \
    mkdir -p /opt && \
    tar -xf /tmp/zephyr-sdk.tar.xz -C /opt && \
    mv /opt/zephyr-sdk-${ZEPHYR_SDK_VERSION} ${ZEPHYR_SDK_INSTALL_DIR} && \
    rm /tmp/zephyr-sdk.tar.xz && \
    cd ${ZEPHYR_SDK_INSTALL_DIR} && \
    ./setup.sh -h -c

# ==============================================================================
# West (Zephyr Meta-tool)
# ==============================================================================
RUN pip3 install --no-cache-dir \
      west \
      pyelftools \
      pyyaml \
      pykwalify \
      canopen \
      packaging \
      progress \
      psutil \
      pylink-square \
      pyserial \
      requests \
      tabulate \
      intelhex

# ==============================================================================
# Environment
# ==============================================================================
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV PATH="${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin:${PATH}"
ENV PATH="${ZEPHYR_SDK_INSTALL_DIR}/riscv64-zephyr-elf/bin:${PATH}"

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;31m\][ZEPHYR] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc && \
    echo 'alias zb="west build"' >> /home/${USER}/.bashrc && \
    echo 'alias zf="west flash"' >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN test -f ${ZEPHYR_SDK_INSTALL_DIR}/sdk_version && \
    arm-zephyr-eabi-gcc --version && \
    west --version && \
    echo "Zephyr image validation: OK (v${ZEPHYR_SDK_VERSION})"

USER ${USER}
WORKDIR /home/${USER}
