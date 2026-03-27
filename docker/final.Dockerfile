# ==============================================================================
# final.Dockerfile - Artifact packaging and extraction stage
#
# Collects framework build outputs from all builder images and packages them
# into versioned distribution tarballs.
#
# Artifacts (12 packages):
#   Library packages (10) -- framework builds per target architecture
#     apex-csf-{VER}-x86_64-linux.tar.gz
#     apex-csf-{VER}-x86_64-linux-cuda.tar.gz
#     apex-csf-{VER}-aarch64-jetson.tar.gz
#     apex-csf-{VER}-aarch64-rpi.tar.gz
#     apex-csf-{VER}-riscv64-linux.tar.gz
#     apex-csf-{VER}-stm32.tar.gz
#     apex-csf-{VER}-arduino.tar.gz
#     apex-csf-{VER}-pico.tar.gz
#     apex-csf-{VER}-esp32.tar.gz
#     apex-csf-{VER}-c2000.tar.gz
#
#   Tool packages (2) -- CLI tools for operators and developers
#     apex-tools-{VER}-x86_64-linux.tar.gz
#     apex_py_tools-{VER}-py3-none-any.whl
#
# Usage:
#   docker compose build final
#   make artifacts
# ==============================================================================
FROM busybox:latest

ARG USER
ARG VERSION=0.0.0

LABEL org.opencontainers.image.title="apex.final" \
      org.opencontainers.image.description="Packaged build artifacts for distribution"

WORKDIR /output

# ==============================================================================
# Collect Framework Builds -- POSIX Targets
# ==============================================================================
COPY --from=apex.builder.cpu:latest     /home/${USER}/workspace/build/native-linux-release/    ./cpu/
COPY --from=apex.builder.cuda:latest    /home/${USER}/workspace/build/native-linux-release/    ./cuda/
COPY --from=apex.builder.jetson:latest  /home/${USER}/workspace/build/jetson-aarch64-release/  ./jetson/
COPY --from=apex.builder.rpi:latest     /home/${USER}/workspace/build/rpi-aarch64-release/     ./rpi/
COPY --from=apex.builder.riscv64:latest /home/${USER}/workspace/build/riscv64-linux-release/   ./riscv64/

# ==============================================================================
# Collect Framework Builds -- MCU Targets
# ==============================================================================
COPY --from=apex.builder.stm32:latest   /home/${USER}/workspace/build/stm32/   ./stm32/
COPY --from=apex.builder.arduino:latest /home/${USER}/workspace/build/arduino/ ./arduino/
COPY --from=apex.builder.pico:latest    /home/${USER}/workspace/build/pico/    ./pico/
COPY --from=apex.builder.esp32:latest   /home/${USER}/workspace/build/esp32/   ./esp32/
COPY --from=apex.builder.c2000:latest   /home/${USER}/workspace/build/c2000/   ./c2000/

# ==============================================================================
# Collect CLI Tools from CPU Builder
# ==============================================================================
COPY --from=apex.builder.cpu:latest /home/${USER}/workspace/build/native-linux-release/bin/tools/ ./tools-bin/
COPY --from=apex.builder.cpu:latest /home/${USER}/workspace/build/native-linux-release/apex_csf-wheels/ ./tools-py/

# ==============================================================================
# Create Distribution Tarballs
# ==============================================================================
RUN tar -czf "apex-csf-${VERSION}-x86_64-linux.tar.gz"      ./cpu/      && \
    tar -czf "apex-csf-${VERSION}-x86_64-linux-cuda.tar.gz" ./cuda/     && \
    tar -czf "apex-csf-${VERSION}-aarch64-jetson.tar.gz"    ./jetson/   && \
    tar -czf "apex-csf-${VERSION}-aarch64-rpi.tar.gz"       ./rpi/      && \
    tar -czf "apex-csf-${VERSION}-riscv64-linux.tar.gz"     ./riscv64/  && \
    tar -czf "apex-csf-${VERSION}-stm32.tar.gz"             ./stm32/    && \
    tar -czf "apex-csf-${VERSION}-arduino.tar.gz"           ./arduino/  && \
    tar -czf "apex-csf-${VERSION}-pico.tar.gz"              ./pico/     && \
    tar -czf "apex-csf-${VERSION}-esp32.tar.gz"             ./esp32/    && \
    tar -czf "apex-csf-${VERSION}-c2000.tar.gz"             ./c2000/    && \
    tar -czf "apex-tools-${VERSION}-x86_64-linux.tar.gz"    ./tools-bin/ && \
    cp ./tools-py/*.whl "apex_py_tools-${VERSION}-py3-none-any.whl"

# ==============================================================================
# Default: List Available Artifacts
# ==============================================================================
CMD ["ls", "-la", "/output"]
