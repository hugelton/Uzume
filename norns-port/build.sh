#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
# Usage: norns-port/build.sh        incremental build of norns (--desktop) in a persistent arm64 trixie container "uzume-nb"
# 1. patched copy of third_party/norns (no .git) -> scratch ctx;  2. image (deps + libmonome) built once;
# 3. ctx streamed into the container with tar, waf configure/build there (build dir persists between runs).
set -e
HERE=$(cd "$(dirname "$0")" && pwd); TP="$HERE/../third_party"
CTX=${CTX:-$HERE/../build/ctx}   # scratch copy lives on the SSD, next to the repository
rm -rf "$CTX"; mkdir -p "$CTX"
tar -C "$TP" --exclude=.git -cf - norns libmonome | tar -C "$CTX" -xf -
# libmonome finds its protocol modules through MONOME_PROTOCOL_DIR (grids work without /usr/local/lib/monome)
patch -p1 -d "$CTX/libmonome" < "$HERE/libmonome-protocol-dir.patch"
python3 "$HERE/apply_patches.py" "$CTX/norns"
# norns ships waf 2.0.14 (2018), which imports `imp` (removed in Python 3.12); libmonome ships a newer waf
cp "$TP/libmonome/waf" "$CTX/norns/waf"
docker build --platform linux/arm64 -f "$HERE/Dockerfile" -t uzume-norns "$CTX" >/dev/null
docker inspect uzume-nb >/dev/null 2>&1 || docker run -d --name uzume-nb --platform linux/arm64 uzume-norns sleep infinity >/dev/null
# overwrite sources, keep build/ and .lock-waf
tar -C "$CTX/norns" -cf - . | docker exec -i uzume-nb tar -C /src/norns --no-same-owner -xf -
docker exec -w /src/norns uzume-nb sh -c 'rm -rf .waf3-2.0.14-*; CXXFLAGS="$(pkg-config --cflags glib-2.0)" ./waf configure --desktop >/dev/null && ./waf -j4'
