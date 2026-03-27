# ==============================================================================
# dev/esp32.Dockerfile - ESP32 (Xtensa/RISC-V) shell with ESP-IDF
#
# Usage:
#   make shell-dev-esp32
#   docker compose run dev-esp32
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER
ARG ESP_IDF_VERSION=v5.5.3

LABEL org.opencontainers.image.title="apex.dev.esp32" \
      org.opencontainers.image.description="ESP32 shell with ESP-IDF"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# ESP-IDF Host Dependencies (was toolchain/esp32.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      flex bison gperf \
      python3-venv python3-serial \
      libffi-dev libssl-dev \
      dfu-util libusb-1.0-0

# ==============================================================================
# Flash Tools
# ==============================================================================
RUN --mount=type=cache,target=/root/.cache/pip \
    pip3 install --break-system-packages esptool pyserial

# ==============================================================================
# ESP-IDF Framework
# ==============================================================================
ENV IDF_PATH=/opt/esp-idf
ENV IDF_TOOLS_PATH=/opt/espressif

RUN set -euo pipefail && \
    mkdir -p "${IDF_TOOLS_PATH}" && \
    git clone --branch "${ESP_IDF_VERSION}" --depth 1 --recursive --shallow-submodules \
      https://github.com/espressif/esp-idf.git "${IDF_PATH}" && \
    cd "${IDF_PATH}" && ./install.sh && \
    chmod -R a+rX "${IDF_TOOLS_PATH}"

# ==============================================================================
# Stable Tool Paths
# ==============================================================================
RUN ln -sf /opt/espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin /opt/xtensa-bin && \
    ln -sfn /opt/espressif/python_env/idf*_py* /opt/idf-venv

ENV PATH="/opt/xtensa-bin:/opt/idf-venv/bin:${PATH}"

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;35m\][ESP32] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN esptool.py version && \
    /opt/xtensa-bin/xtensa-esp-elf-gcc --version | head -1 && \
    /opt/idf-venv/bin/python -m kconfgen --help >/dev/null 2>&1 && \
    echo "ESP32 image validation: OK (ESP-IDF ${ESP_IDF_VERSION})"

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Source ESP-IDF environment:
#   . $IDF_PATH/export.sh
#
# Build and flash:
#   idf.py set-target esp32
#   idf.py build
#   idf.py -p /dev/ttyUSB0 flash monitor
# ==============================================================================
