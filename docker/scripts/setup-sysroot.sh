#!/usr/bin/env bash
# ==============================================================================
# setup-sysroot.sh - install foreign-arch sysroot libraries + mold cross-linker
#
# Installs the runtime/dev libraries the apex cross builds link against, suffixed
# for the foreign architecture, and creates the <triple>-ld.mold symlink GCC
# cross-compilers look for. Shared by the cross-Linux dev images so the library
# set is defined once (no per-image drift). Run under an apt cache mount.
#
# Usage: setup-sysroot <debian-arch> <gnu-triple>
#        e.g. setup-sysroot arm64 aarch64-linux-gnu
# ==============================================================================
set -euo pipefail
arch="${1:?usage: setup-sysroot <debian-arch> <gnu-triple>}"
triple="${2:?usage: setup-sysroot <debian-arch> <gnu-triple>}"

# tcmalloc's package name diverged at the time64 transition: riscv64 ships the
# t64 variant, the older arches keep the legacy name.
case "$arch" in
riscv64) tcmalloc="libtcmalloc-minimal4t64" ;;
*) tcmalloc="libtcmalloc-minimal4" ;;
esac

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
  "libgoogle-perftools-dev:${arch}" \
  "${tcmalloc}:${arch}" \
  "libunwind-dev:${arch}" \
  "libssl-dev:${arch}" \
  "zlib1g-dev:${arch}" \
  "liblapacke-dev:${arch}" \
  "libopenblas-dev:${arch}" \
  "libsuitesparse-dev:${arch}"

# GCC cross-compilers invoke <triple>-ld.mold for -fuse-ld=mold, not the bare
# mold binary; the symlink makes mold usable for the cross link.
ln -sfn /usr/bin/mold "/usr/bin/${triple}-ld.mold"
