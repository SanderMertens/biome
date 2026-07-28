#!/usr/bin/env bash
#
# Render every scene in SCENES to test/out/<scene>.ppm and compare it against
# the baseline in test/scenes/<scene>.ppm.
#
# The unit tests cover simulation rules, but a lot of what breaks when the
# engine changes is only visible in a frame: props that move because entity
# ids shifted, assets that lose their transform, lighting that drifts. This
# catches that.
#
# Regenerate the baselines with: ./test_screenshots_baseline.sh
#
# Usage: ./test_screenshots.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

source ./test_screenshots_common.sh

echo "==> Building biome"
bake3 build biome --local-env

if [[ ! -d "$BASELINE_DIR" ]]; then
  echo "error: no baselines in $BASELINE_DIR. Run ./test_screenshots_baseline.sh first." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.ppm

failures=()
passed=0

for name in "${SCENES[@]}"; do
  scene="$SCENES_DIR/$name.flecs"
  out="$OUT_DIR/$name.ppm"
  baseline="$BASELINE_DIR/$name.ppm"

  echo "==> $name"

  if ! render_scene "$scene" "$out"; then
    echo "    RENDER FAILED" >&2
    failures+=("$name (render)")
    continue
  fi

  if [[ ! -f "$baseline" ]]; then
    echo "    no baseline at $baseline" >&2
    failures+=("$name (no baseline)")
    continue
  fi

  diff_bytes=$({ cmp -l "$baseline" "$out" 2>/dev/null || true; } | wc -l | tr -d ' ')
  total_bytes=$(wc -c < "$baseline" | tr -d ' ')
  pct=$(awk "BEGIN { printf \"%.2f\", ($diff_bytes/$total_bytes)*100 }")

  if awk "BEGIN { exit !(($diff_bytes/$total_bytes)*100 < $THRESHOLD_PCT) }"; then
    echo "    ok (${pct}% diff)"
    passed=$((passed + 1))
  else
    echo "    MISMATCH (${pct}% diff, baseline=$baseline current=$out)" >&2
    failures+=("$name (${pct}% diff)")
  fi
done

echo
if [[ ${#failures[@]} -eq 0 ]]; then
  echo "All ${passed} scene(s) match their baseline."
else
  echo "${#failures[@]} scene(s) failed:" >&2
  for f in "${failures[@]}"; do
    echo "  - $f" >&2
  done
  echo >&2
  echo "If the change is intended, refresh with ./test_screenshots_baseline.sh" >&2
  exit 1
fi
