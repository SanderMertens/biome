#!/usr/bin/env bash
#
# Render a baseline frame for every scene and store it under test/scenes.
# Run this after an intended change to what the game looks like, then commit
# the updated images along with the change.
#
# Usage: ./test_screenshots_baseline.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

source ./test_screenshots_common.sh

echo "==> Building biome"
bake3 build biome --local-env

mkdir -p "$BASELINE_DIR"

failures=()

for name in "${SCENES[@]}"; do
  scene="$SCENES_DIR/$name.flecs"
  out="$BASELINE_DIR/$name.ppm"

  echo "==> Rendering $name -> $out"
  if render_scene "$scene" "$out"; then
    echo "    ok"
  else
    echo "    FAILED" >&2
    failures+=("$name")
  fi
done

echo
if [[ ${#failures[@]} -eq 0 ]]; then
  echo "All ${#SCENES[@]} scene(s) rendered successfully."
else
  echo "${#failures[@]} scene(s) failed:" >&2
  for f in "${failures[@]}"; do
    echo "  - $f" >&2
  done
  exit 1
fi
