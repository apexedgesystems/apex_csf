# ==============================================================================
# dev/stm32.Dockerfile - STM32 (ARM Cortex-M) bare-metal shell
#
# Usage:
#   make shell-dev-stm32
#   docker compose run dev-stm32
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER

LABEL org.opencontainers.image.title="apex.dev.stm32" \
      org.opencontainers.image.description="STM32 bare-metal shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# ARM Compiler Toolchain (was toolchain/stm32.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      gcc-arm-none-eabi \
      libnewlib-arm-none-eabi

ENV PATH="/usr/bin/arm-none-eabi:${PATH}"

# ==============================================================================
# Flash and Debug Tools
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      stlink-tools \
      openocd

# ==============================================================================
# STM32Cube HAL Libraries
# ==============================================================================
RUN git clone --recursive --depth 1 \
      https://github.com/STMicroelectronics/STM32CubeF4.git /opt/STM32CubeF4 && \
    git clone --recursive --depth 1 \
      https://github.com/STMicroelectronics/STM32CubeL4.git /opt/STM32CubeL4

ENV STM32CUBE_F4_PATH="/opt/STM32CubeF4"
ENV STM32CUBE_L4_PATH="/opt/STM32CubeL4"

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;33m\][STM32] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN arm-none-eabi-gcc --version && \
    st-flash --version && \
    openocd --version 2>&1 | head -1 && \
    echo "STM32 image validation: OK"

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Compile (replace cortex-mX with your core):
#   arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -T linker.ld \
#       -o build/output.elf src/*.c
#
# Flash with ST-Link:
#   st-flash write build/output.bin 0x08000000
#
# Flash with OpenOCD:
#   openocd -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
#     -c "program build/output.elf verify reset exit"
# ==============================================================================
