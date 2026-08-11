#!/usr/bin/env bash
# Build the browser engine and drop it next to the shell.
#
#   scripts/build-web.sh          incremental
#   scripts/build-web.sh clean    reconfigure from scratch
set -euo pipefail
cd "$(dirname "$0")/.."

[ "${1:-}" = "clean" ] && rm -rf build/web

emcmake cmake -S . -B build/web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DREVC_AUDIO=OAL -DREVC_WITH_OPUS=OFF \
  -DLIBRW_TOOLS=OFF -DLIBRW_INSTALL=OFF -DLIBRW_EXAMPLES=OFF

ninja -C build/web

mkdir -p web/public
cp build/web/src/reVC.js build/web/src/reVC.wasm web/public/
ls -la web/public/reVC.js web/public/reVC.wasm
