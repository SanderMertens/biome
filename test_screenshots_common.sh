#!/usr/bin/env bash
#
# Shared configuration for the screenshot tests. Sourced by
# test_screenshots.sh and test_screenshots_baseline.sh.

BIOME="./.bake/local_env/build/biome/arm64-$(uname -s)-debug/biome"
SCENES_DIR="etc/scenes"
BASELINE_DIR="test/scenes"
OUT_DIR="test/out"

# Scenes that can be loaded on their own. The other files in etc/scenes are
# includes and have no camera of their own.
SCENES=(biome test_power)

# Frames to simulate before capturing. Terrain generation, scattering and the
# first buildings all have to settle, or the frame captures a half-built world.
FRAME_NTH=120

WIDTH=640
HEIGHT=400

# Renders are deterministic on the same machine, but leave room for driver
# differences rather than demanding an exact match.
THRESHOLD_PCT=0.1

render_scene() {
  local scene="$1" out="$2"
  "$BIOME" \
    --scene "$scene" \
    --frame-out "$out" \
    --frame-nth "$FRAME_NTH" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    > /dev/null
}
