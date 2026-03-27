# ==============================================================================
# dev/c2000.Dockerfile - TI C2000 DSP shell
#
# Usage:
#   make shell-dev-c2000
#   docker compose run dev-c2000
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER
ARG TI_CGT_VERSION=22.6.2.LTS
# UniFlash 8.4 is compatible with XDS110 firmware v2.3.x (factory default).
# UniFlash 9.x requires v3.0+ firmware which needs a DFU update that can be
# flaky over USB hubs. Use 8.4 to avoid the firmware mismatch.
ARG UNIFLASH_VERSION=8.4.0.4458

LABEL org.opencontainers.image.title="apex.dev.c2000" \
      org.opencontainers.image.description="TI C2000 DSP shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# TI C2000 Code Generation Tools (was toolchain/c2000.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      ca-certificates wget libusb-1.0-0

RUN mkdir -p /opt/ti && \
    cd /opt/ti && \
    wget --progress=dot:giga -O cgt-installer.bin \
      "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-xqxJ05PLfM/${TI_CGT_VERSION}/ti_cgt_c2000_${TI_CGT_VERSION}_linux-x64_installer.bin" && \
    chmod +x cgt-installer.bin && \
    ./cgt-installer.bin --mode unattended --prefix /opt/ti && \
    rm -f cgt-installer.bin && \
    ln -sfn /opt/ti/ti-cgt-c2000_${TI_CGT_VERSION} /opt/ti/c2000-cgt

ENV PATH="/opt/ti/c2000-cgt/bin:${PATH}"
ENV C2000_CGT_ROOT="/opt/ti/c2000-cgt"

# ==============================================================================
# C2000Ware SDK
# ==============================================================================
RUN cd /opt/ti && \
    git clone --depth 1 https://github.com/TexasInstruments/c2000ware-core-sdk.git

ENV C2000WARE_ROOT="/opt/ti/c2000ware-core-sdk"

# ==============================================================================
# UniFlash Programming Tool
# ==============================================================================
RUN cd /opt/ti && \
    wget --progress=dot:giga -O uniflash-installer.run \
      "https://dr-download.ti.com/software-development/software-programming-tool/MD-QeJBJLj8gq/${UNIFLASH_VERSION%.*}/uniflash_sl.${UNIFLASH_VERSION}.run" && \
    chmod +x uniflash-installer.run && \
    ./uniflash-installer.run --mode unattended --prefix /opt/ti/uniflash && \
    rm -f uniflash-installer.run

ENV PATH="/opt/ti/uniflash:${PATH}"

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;33m\][C2000] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN cl2000 --version && \
    echo "C2000 image validation: OK"

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Compile:
#   cl2000 --silicon_version=28 -O2 -o output.obj source.c
#
# Flash with UniFlash CLI:
#   /opt/ti/uniflash/dslite.sh --config=myconfig.ccxml -f output.out
# ==============================================================================
