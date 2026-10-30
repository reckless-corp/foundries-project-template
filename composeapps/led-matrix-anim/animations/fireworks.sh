#!/bin/sh
# Run on the host; playback continues on the MCU after this script exits.
set -eu
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [repeat-count: 0 loops forever (default)]" >&2
    exit 2
fi

# Locate the bundled compose file even when invoked from another directory.
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# A rocket rises, bursts into expanding sparks, then falls and fades away.
# First burst:    Expanding:      Falling sparks:
# .............  ......#......   .............
# ......#......  ....#...#....   ..#.......#..
# .....###.....  ...#..#..#...   .............
# ......#......  ....#...#....   #...........#
# .............  ......#......   .............
# .............  .............   ..#.......#..
# .............  .............   ......#......
# .............  .............   .............
docker compose -f "$app_dir/docker-compose.yml" -p "${COMPOSE_PROJECT_NAME:-led-matrix-anim-app}" \
    exec -T matrix led-matrix-animctl ANIM "${1:-0}" \
    100:00000000000000000000000040004000 \
    100:00000000000000004000400000000000 \
    100:00000000400040000000000000000000 \
    100:00004000e00040000000000000000000 \
    130:40001001480210014000000000000000 \
    160:08020000020800000802400000000000 \
    180:00000404000001100000040440000000 \
    220:00000000000000000110000004044000 \
    450:00000000000000000000000000000000

# Make the new animation visible even if DISPLAY OFF was used earlier.
exec docker compose -f "$app_dir/docker-compose.yml" -p "${COMPOSE_PROJECT_NAME:-led-matrix-anim-app}" \
    exec -T matrix led-matrix-animctl DISPLAY ON
