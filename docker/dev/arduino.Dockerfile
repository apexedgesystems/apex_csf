# ==============================================================================
# dev/arduino.Dockerfile - Arduino (AVR) shell
#
# Uses Arduino's avr-gcc 7.3 (C++17) instead of system gcc-avr (5.4, C++14).
#
# Usage:
#   make shell-dev-arduino
#   docker compose run dev-arduino
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER
ARG ARDUINO_CLI_VERSION=0.35.0

LABEL org.opencontainers.image.title="apex.dev.arduino" \
      org.opencontainers.image.description="Arduino (AVR) shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# Build Dependencies
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      curl \
      tar

# ==============================================================================
# Arduino CLI (was toolchain/arduino.Dockerfile)
# ==============================================================================
RUN curl -fsSL "https://downloads.arduino.cc/arduino-cli/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz" \
      | tar -xz -C /usr/local/bin && \
    chmod +x /usr/local/bin/arduino-cli

# ==============================================================================
# AVR Compiler Toolchain
# ==============================================================================
RUN arduino-cli config init && \
    arduino-cli core update-index && \
    arduino-cli core install arduino:avr && \
    AVR_GCC_DIR=$(find /root/.arduino15/packages/arduino/tools/avr-gcc -maxdepth 1 -mindepth 1 -type d | head -1) && \
    cp -a "${AVR_GCC_DIR}" /opt/avr-toolchain && \
    rm -rf /root/.arduino15

ENV PATH="/opt/avr-toolchain/bin:${PATH}"

# ==============================================================================
# Flash Tools
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      avrdude

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;31m\][ARDUINO] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN avr-gcc --version | head -1 && \
    avr-g++ -dumpversion && \
    avrdude -? 2>&1 | head -1 && \
    echo "Arduino image validation: OK"

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Bare-metal (no Arduino core):
#   avr-gcc -mmcu=atmega328p -Os -o build/out.elf src/*.c
#   avr-objcopy -O ihex -R .eeprom build/out.elf build/out.hex
#   avrdude -c arduino -p m328p -P /dev/ttyUSB0 -b 115200 -U flash:w:build/out.hex:i
# ==============================================================================
