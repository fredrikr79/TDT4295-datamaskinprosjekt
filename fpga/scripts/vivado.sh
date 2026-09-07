#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "vivado.sh starting..."
echo "Project root: $PROJECT_ROOT"

if [[ -f "$PROJECT_ROOT/.vivado.env" ]]; then
    source "$PROJECT_ROOT/.vivado.env"
else
    echo "ERROR: Missing $PROJECT_ROOT/.vivado.env"
    exit 1
fi

echo "Container: $VIVADO_CONTAINER"
echo "Vivado settings: $VIVADO_SETTINGS"
echo "Arguments: $*"

# Directory for all Vivado-generated files
VIVADO_WORK_DIR="$PROJECT_ROOT/build/vivado"
mkdir -p "$VIVADO_WORK_DIR"

# Build a safely quoted command for the container.
cmd="cd $(printf '%q' "$VIVADO_WORK_DIR") && source $(printf '%q' "$VIVADO_SETTINGS") && exec vivado"

for arg in "$@"; do
    cmd+=" $(printf '%q' "$arg")"
done

echo "Launching Vivado in Distrobox..."
echo "Working directory: $VIVADO_WORK_DIR"

exec distrobox-enter \
    --name "$VIVADO_CONTAINER" \
    -- bash -lc "$cmd"