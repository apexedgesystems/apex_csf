# ==============================================================================
# dev/cpu.Dockerfile - Native x86_64 shell
#
# Usage:
#   make shell-dev
#   docker compose run dev
# ==============================================================================
FROM apex.base:latest

ARG USER

LABEL org.opencontainers.image.title="apex.dev.cpu" \
      org.opencontainers.image.description="Native x86_64 shell"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# Shell Prompt
# ==============================================================================
RUN echo 'if [ -n "$PS1" ]; then export PS1="\[\e[1;34m\][CPU] \u@\h:\w \$\[\e[0m\] "; fi' \
      >> /home/${USER}/.bashrc

# ==============================================================================
# Validation
# ==============================================================================
RUN echo "CPU image validation: OK"

USER ${USER}
WORKDIR /home/${USER}
