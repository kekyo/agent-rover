#!/usr/bin/env bash
set -euo pipefail

repository=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
definition="$repository/agents/toolchain/Containerfile"
digest=$(sha256sum "$definition")
image="localhost/agent-rover-windows-toolchain:${digest%% *}"

if ! command -v podman >/dev/null 2>&1; then
    echo 'Podman is required. Install Podman and run ./prereq.sh.' >&2
    exit 1
fi

case "${1:-}" in
    --prepare)
        if podman image exists "$image"; then
            echo "Reusing Windows toolchain: $image"
        else
            podman build --platform linux/amd64 --tag "$image" \
                --file "$definition" "$repository/agents/toolchain"
        fi
        exit 0
        ;;
    --image)
        echo "$image"
        exit 0
        ;;
    '')
        echo 'Usage: windows-toolchain.sh COMMAND [ARG...] | --prepare | --image' >&2
        exit 2
        ;;
esac

if ! image_id=$(podman image inspect --format '{{.Id}}' "$image" 2>/dev/null); then
    echo 'The pinned Windows toolchain is not prepared. Run ./prereq.sh from the repository root.' >&2
    exit 1
fi

# Keep absolute paths stable for dependency files and custom build directories.
# Shared SELinux labels allow concurrent compiler containers on this checkout.
mounts=(--volume "$repository:$repository:rw,z")
if [[ -n "${AGENT_ROVER_BUILD_DIR:-}" ]]; then
    build_directory=$(cd -- "$AGENT_ROVER_BUILD_DIR" && pwd -P)
    mounts+=(--volume "$build_directory:$build_directory:rw,z")
fi
exec podman run --rm --pull=never --network=none --userns=keep-id \
    "${mounts[@]}" --workdir "$(pwd -P)" "$image_id" "$@"
