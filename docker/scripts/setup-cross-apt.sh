#!/usr/bin/env bash
# ==============================================================================
# setup-cross-apt.sh - configure apt for cross-architecture package installs
#
# Pins the native amd64 repos (security suite on security.ubuntu.com), adds the
# foreign architecture from ports.ubuntu.com, and enables it for dpkg. Shared by
# every cross-Linux dev image (arm64 for rpi/jetson, riscv64 for riscv64) so the
# repo layout lives in one place.
#
# Usage: setup-cross-apt <debian-arch>   e.g. setup-cross-apt arm64
# ==============================================================================
set -euo pipefail
arch="${1:?usage: setup-cross-apt <debian-arch>}"

# Native amd64 repos -- replace the deb822 default so the [arch=] pins apply.
rm -f /etc/apt/sources.list.d/ubuntu.sources
cat >/etc/apt/sources.list <<'EOF'
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main restricted universe multiverse
deb [arch=amd64] http://security.ubuntu.com/ubuntu noble-security main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-backports main restricted universe multiverse
EOF

# Foreign arch -- all suites live on ports.ubuntu.com.
for suite in "" -updates -security -backports; do
  echo "deb [arch=${arch}] http://ports.ubuntu.com/ubuntu-ports noble${suite} main restricted universe multiverse"
done >"/etc/apt/sources.list.d/ubuntu-${arch}-ports.list"

dpkg --add-architecture "${arch}"
