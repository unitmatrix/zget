#!/usr/bin/env bash
set -euo pipefail

# The Azure mirror has repeatedly stalled GitHub-hosted runners. Remove it from
# this disposable runner's mirror list and let APT use the remaining mirrors.
sudo sed -i '\|azure.archive.ubuntu.com|d' /etc/apt/apt-mirrors.txt

sudo apt-get update

# Keep one authoritative dependency list for normal, sanitizer, fuzz, and
# release builds. Callers may append job-specific tools such as clang.
packages=(libcurl4-openssl-dev pkg-config zlib1g-dev)
packages+=("$@")
sudo apt-get install -y "${packages[@]}"
