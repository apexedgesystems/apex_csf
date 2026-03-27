# ==============================================================================
# dev/pico.Dockerfile - Raspberry Pi Pico (RP2040) shell
#
# Uses the official ARM GNU Toolchain because Pico SDK 2.x requires ld >= 2.40.
#
# Usage:
#   make shell-dev-pico
#   docker compose run dev-pico
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER
ARG PICO_SDK_VERSION=2.1.0
ARG ARM_TOOLCHAIN_VERSION=13.3.rel1

LABEL org.opencontainers.image.title="apex.dev.pico" \
      org.opencontainers.image.description="Raspberry Pi Pico (RP2040) shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# ARM GNU Toolchain (was toolchain/pico.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      libusb-1.0-0-dev \
      pkg-config \
      xz-utils \
      wget

RUN wget -q "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-x86_64-arm-none-eabi.tar.xz" \
      -O /tmp/arm-toolchain.tar.xz && \
    mkdir -p /opt/arm-gnu-toolchain && \
    tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-gnu-toolchain --strip-components=1 && \
    rm /tmp/arm-toolchain.tar.xz

ENV PATH="/opt/arm-gnu-toolchain/bin:${PATH}"

# ==============================================================================
# Pico SDK
# ==============================================================================
RUN git clone --depth=1 -b "${PICO_SDK_VERSION}" https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk && \
    cd /opt/pico-sdk && \
    git submodule update --init

ENV PICO_SDK_PATH=/opt/pico-sdk

# ==============================================================================
# Flash Tools (picotool)
# ==============================================================================
RUN git clone https://github.com/raspberrypi/picotool.git /tmp/picotool && \
    cd /tmp/picotool && \
    mkdir -p build && cd build && \
    cmake .. -DPICO_SDK_PATH="${PICO_SDK_PATH}" -DCMAKE_INSTALL_PREFIX=/usr/local && \
    make -j"$(nproc)" && \
    make install && \
    ldconfig && \
    rm -rf /tmp/picotool

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;37m\][PICO] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc && \
    echo 'export PICO_SDK_PATH=/opt/pico-sdk' >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN arm-none-eabi-gcc --version && \
    arm-none-eabi-ld --version | head -1 && \
    /usr/local/bin/picotool version && \
    echo "Pico image validation: OK"

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Build:
#   cmake -B build -S . -G Ninja -DPICO_SDK_PATH=${PICO_SDK_PATH}
#   cmake --build build -j
#
# Flash:
#   picotool load build/firmware.uf2 -f
# ==============================================================================
