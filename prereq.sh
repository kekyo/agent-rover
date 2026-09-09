#!/usr/bin/env bash
set -euo pipefail

repository=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
exec "$repository/agents/scripts/windows-toolchain.sh" --prepare
