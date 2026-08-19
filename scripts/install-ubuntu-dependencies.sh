#!/usr/bin/env bash
set -euo pipefail

# Keep GitHub's normal mirror order: the Azure-local mirror is fastest when it
# is healthy, and the runner image already provides public fallbacks. Short
# acquisition timeouts let APT reach those fallbacks promptly during a mirror
# outage instead of occupying a serialized CI matrix for a long time. Two
# retries tolerate brief connection resets without leaving a persistent mirror
# failure unbounded.
apt_options=(
    -o Acquire::Retries=2
    -o Acquire::http::Timeout=20
    -o Acquire::https::Timeout=20
)
sudo apt-get "${apt_options[@]}" update

# Keep one authoritative dependency list for normal, sanitizer, fuzz, and
# release builds. Callers may append job-specific tools such as clang.
packages=(libcurl4-openssl-dev pkg-config zlib1g-dev)
packages+=("$@")
sudo env DEBIAN_FRONTEND=noninteractive \
    apt-get "${apt_options[@]}" install -y --no-install-recommends "${packages[@]}"
