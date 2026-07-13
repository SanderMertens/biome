#!/usr/bin/env sh
set -eu
bake3 build biome --local-env
./.bake/local_env/build/biome/arm64-Darwin-debug/biome "$@"
