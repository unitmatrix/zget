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

# APT's acquisition timeouts apply to individual connections, not to the
# complete update operation. Bound the whole command as well, otherwise one
# wedged index download can still consume the entire workflow job.
apt_command_timeout=2m
run_apt_update() {
    sudo timeout --kill-after=10s "$apt_command_timeout" \
        apt-get "${apt_options[@]}" update
}

# Try the runner's normal mirror list first so the Azure-local mirror remains
# the fast path whenever it is healthy. If the complete update fails or times
# out, remove Azure only from this disposable runner and retry with the public
# mirrors that GitHub already placed later in the list.
if ! run_apt_update; then
    mirror_list=/etc/apt/apt-mirrors.txt
    if [[ ! -f "$mirror_list" ]]; then
        echo "APT update failed and $mirror_list is unavailable for fallback" >&2
        exit 1
    fi

    echo "::warning::Azure Ubuntu mirror failed; retrying with public mirrors"
    sudo sed -i '\|azure.archive.ubuntu.com|d' "$mirror_list"

    # Show the effective fallback list in the Actions log. This makes it clear
    # which mirrors the retry uses without hiding runner state in this script.
    sudo cat "$mirror_list"
    run_apt_update
fi

# Keep one authoritative dependency list for normal, sanitizer, fuzz, and
# release builds. Callers may append job-specific tools such as clang.
packages=(libcurl4-openssl-dev pkg-config zlib1g-dev)
packages+=("$@")
sudo timeout --kill-after=10s "$apt_command_timeout" \
    env DEBIAN_FRONTEND=noninteractive \
    apt-get "${apt_options[@]}" install -y --no-install-recommends "${packages[@]}"
