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
    echo "       Copy .vivado_example.env to .vivado.env and adjust it for this machine."
    exit 1
fi

VIVADO_CMD="${VIVADO_CMD:-vivado}"
VIVADO_SETTINGS="${VIVADO_SETTINGS:-}"
VIVADO_CONTAINER="${VIVADO_CONTAINER:-}"
VIVADO_LAUNCHER="${VIVADO_LAUNCHER:-}"

# Directory for all Vivado-generated files
VIVADO_WORK_DIR="$PROJECT_ROOT/build/vivado"
mkdir -p "$VIVADO_WORK_DIR"

echo "Vivado command:  $VIVADO_CMD"
echo "Vivado settings: ${VIVADO_SETTINGS:-<none>}"
echo "Container:       ${VIVADO_CONTAINER:-<none>}"
echo "Launcher:        ${VIVADO_LAUNCHER:-<none>}"
echo "Arguments:       $*"
echo "Working dir:     $VIVADO_WORK_DIR"

# Build a safely quoted command line to run inside whichever environment
# ends up hosting Vivado.
cmd="cd $(printf '%q' "$VIVADO_WORK_DIR")"

if [[ -n "$VIVADO_SETTINGS" ]]; then
    cmd+=" && source $(printf '%q' "$VIVADO_SETTINGS")"
fi

cmd+=" && exec $(printf '%q' "$VIVADO_CMD")"

# Vivado is started from $VIVADO_WORK_DIR, so any argument that names a file
# relative to the caller's directory (e.g. -source fpga/scripts/build.tcl)
# has to be resolved to an absolute path first.
for arg in "$@"; do
    if [[ "$arg" != /* && -e "$arg" ]]; then
        arg="$(cd "$(dirname "$arg")" && pwd)/$(basename "$arg")"
    fi
    cmd+=" $(printf '%q' "$arg")"
done

if [[ -n "$VIVADO_CONTAINER" ]]; then
    echo "Launching Vivado in Distrobox container '$VIVADO_CONTAINER'..."
    exec distrobox-enter --name "$VIVADO_CONTAINER" -- bash -lc "$cmd"
elif [[ -n "$VIVADO_LAUNCHER" ]]; then
    echo "Launching Vivado via custom launcher..."
    # VIVADO_LAUNCHER is intentionally word-split: it is a command prefix.
    exec $VIVADO_LAUNCHER bash -lc "$cmd"
else
    echo "Launching Vivado natively..."
    exec bash -lc "$cmd"
fi
