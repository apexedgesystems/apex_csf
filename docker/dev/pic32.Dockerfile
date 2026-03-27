# ==============================================================================
# dev/pic32.Dockerfile - PIC32 (MIPS/XC32) shell
#
# XC32 headless install may fail; see pic32prog or MPLAB IPE as alternatives.
#
# Usage:
#   make shell-dev-pic32
#   docker compose run dev-pic32
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER
ARG XC32_URL="https://ww1.microchip.com/downloads/en/DeviceDoc/xc32-v2.50-full-install-linux-installer.run"

LABEL org.opencontainers.image.title="apex.dev.pic32" \
      org.opencontainers.image.description="PIC32 (MIPS/XC32) shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# XC32 Compiler Toolchain (was toolchain/pic32.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      ca-certificates curl xz-utils libncurses5

RUN curl -L -o /tmp/xc32.run "${XC32_URL}" && \
    chmod +x /tmp/xc32.run && \
    /tmp/xc32.run --mode unattended --unattendedmodeui none \
                  --prefix /opt/microchip/xc32 \
                  --netservername "localhost" || \
    echo "XC32 installer returned non-zero exit code" && \
    rm -f /tmp/xc32.run && \
    if ls -d /opt/microchip/xc32/v*/bin >/dev/null 2>&1; then \
      d=$(ls -d /opt/microchip/xc32/v*/bin | sort -V | tail -1) && \
      ln -sfn "$d" /opt/microchip/xc32/bin; \
    else \
      mkdir -p /opt/microchip/xc32/bin; \
    fi

ENV PATH="/opt/microchip/xc32/bin:${PATH}"

# ==============================================================================
# Flash Tools
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    (DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y pic32prog || true)

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;36m\][PIC32] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc && \
    echo 'export PATH="/opt/microchip/xc32/bin:$PATH"' >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN if command -v xc32-gcc >/dev/null 2>&1; then \
      xc32-gcc --version; \
    else \
      echo "xc32-gcc not found - manual installation may be required"; \
    fi && \
    (pic32prog -V 2>&1 | head -1 || echo "pic32prog not available")

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Compile:
#   xc32-gcc -mprocessor=32MX795F512L -O2 -o output.elf src.c
#
# Flash via bootloader:
#   pic32prog -p /dev/ttyUSB0 -b 115200 output.hex
# ==============================================================================
