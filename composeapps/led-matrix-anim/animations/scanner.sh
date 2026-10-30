#!/bin/sh
# Run on the host; playback continues on the MCU after this script exits.
set -eu
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [repeat-count: 0 loops forever (default)]" >&2
    exit 2
fi

# Locate the bundled compose file even when invoked from another directory.
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# A two-column bar sweeps left to right and back (80 ms per step).
docker compose -f "$app_dir/docker-compose.yml" -p "${COMPOSE_PROJECT_NAME:-led-matrix-anim-app}" \
    exec -T matrix led-matrix-animctl ANIM "${1:-0}" \
    80:03000300030003000300030003000300 \
    80:06000600060006000600060006000600 \
    80:0c000c000c000c000c000c000c000c00 \
    80:18001800180018001800180018001800 \
    80:30003000300030003000300030003000 \
    80:60006000600060006000600060006000 \
    80:c000c000c000c000c000c000c000c000 \
    80:80018001800180018001800180018001 \
    80:00030003000300030003000300030003 \
    80:00060006000600060006000600060006 \
    80:000c000c000c000c000c000c000c000c \
    80:00180018001800180018001800180018 \
    80:000c000c000c000c000c000c000c000c \
    80:00060006000600060006000600060006 \
    80:00030003000300030003000300030003 \
    80:80018001800180018001800180018001 \
    80:c000c000c000c000c000c000c000c000 \
    80:60006000600060006000600060006000 \
    80:30003000300030003000300030003000 \
    80:18001800180018001800180018001800 \
    80:0c000c000c000c000c000c000c000c00 \
    80:06000600060006000600060006000600

# Make the new animation visible even if DISPLAY OFF was used earlier.
exec docker compose -f "$app_dir/docker-compose.yml" -p "${COMPOSE_PROJECT_NAME:-led-matrix-anim-app}" \
    exec -T matrix led-matrix-animctl DISPLAY ON
