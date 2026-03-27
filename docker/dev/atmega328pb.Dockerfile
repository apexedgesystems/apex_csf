# ==============================================================================
# dev/atmega328pb.Dockerfile - ATmega328PB (AVR) shell
#
# Usage:
#   make shell-dev-atmega328pb
#   docker compose run dev-atmega328pb
# ==============================================================================
FROM apex.dev.cpu:latest

ARG USER

LABEL org.opencontainers.image.title="apex.dev.atmega328pb" \
      org.opencontainers.image.description="ATmega328PB (AVR) shell"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# AVR Compiler Toolchain (was toolchain/atmega328pb.Dockerfile)
# ==============================================================================
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      gcc-avr \
      avr-libc

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
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;31m\][AVR] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN avr-gcc --version && \
    avrdude -? 2>&1 | head -1 && \
    echo "ATmega328PB image validation: OK"

USER ${USER}
WORKDIR /home/${USER}

# ==============================================================================
# Cheat Sheet
# ==============================================================================
# Compile (ATmega328PB @ 16MHz):
#   avr-gcc -mmcu=atmega328pb -DF_CPU=16000000UL -Os -o build/out.elf src/*.c
#   avr-objcopy -O ihex -R .eeprom build/out.elf build/out.hex
#
# Flash with ISP (e.g., USBtiny):
#   avrdude -c usbtiny -p m328pb -U flash:w:build/out.hex:i
# ==============================================================================
