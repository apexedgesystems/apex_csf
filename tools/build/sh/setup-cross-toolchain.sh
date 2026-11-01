#!/usr/bin/env bash
# ==============================================================================
# setup-cross-toolchain.sh - install a cross-compilation toolchain for one arch
#
# Installs the per-arch GCC/binutils cross packages plus the tooling every
# cross-Linux dev image needs (pkg-config, qemu-user-static, file). Shared by the
# cross-Linux dev images so "what a cross toolchain needs" lives in one place.
# Run under an apt cache mount (the calling RUN provides it).
#
# Usage: setup-cross-toolchain <debian-arch>   e.g. setup-cross-toolchain arm64
# ==============================================================================
set -euo pipefail
arch="${1:?usage: setup-cross-toolchain <debian-arch>}"

case "$arch" in
arm64) compilers="crossbuild-essential-arm64 binutils-aarch64-linux-gnu" ;;
riscv64) compilers="gcc-riscv64-linux-gnu g++-riscv64-linux-gnu binutils-riscv64-linux-gnu" ;;
*)
  echo "setup-cross-toolchain: unsupported arch '$arch'" >&2
  exit 2
  ;;
esac

apt-get update
# shellcheck disable=SC2086  # $compilers is an intentional word-split package list
DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
  $compilers pkg-config qemu-user-static file
