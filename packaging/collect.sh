#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
# Collects what build-deb.sh packs, from the build containers (Docker, arm64 Debian 13):
#   out/deploy/   the norns binary, libmonome, norns' Lua tree and resources   <- after norns-port/build.sh
#   out/pkg-src/  sclang, scsynth, the class library, the UGen plug-ins        <- from the image built with sc/build/Dockerfile
#                 and PipeWire's libjack (the Debian package pipewire-jack, unmodified)
# Usage: sh packaging/collect.sh        (docker images: uzume-sc from sc/build, container uzume-nb from norns-port/build.sh)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
CTX=$ROOT/build/ctx
[ -d "$CTX/norns/lua" ] || { echo "run UZUME_BUFFRAMES=2097152 sh norns-port/build.sh first"; exit 1; }
docker image inspect uzume-sc >/dev/null 2>&1 || { echo "build the SuperCollider image first: docker build --platform linux/arm64 -t uzume-sc -f sc/build/Dockerfile sc/build"; exit 1; }

rm -rf out/deploy out/pkg-src; mkdir -p out/deploy/bin out/deploy/lib out/deploy/home/we/norns out/pkg-src/bin out/pkg-src/lib out/pkg-src/share
docker cp uzume-nb:/src/norns/build/norns/norns out/deploy/bin/norns
docker cp uzume-nb:/usr/local/lib/libmonome.so.1.4.11 out/deploy/lib/ && ln -sf libmonome.so.1.4.11 out/deploy/lib/libmonome.so.1
cp -a "$CTX/norns/lua" "$CTX/norns/resources" "$CTX/norns/LICENSE.txt" "$CTX/norns/matronrc.lua" out/deploy/home/we/norns/

cid=$(docker create uzume-sc); docker cp "$cid":/opt/uzume out/sc-prefix; docker rm "$cid" >/dev/null
cp out/sc-prefix/bin/sclang out/sc-prefix/bin/scsynth out/pkg-src/bin/
cp -a out/sc-prefix/lib/SuperCollider out/pkg-src/lib/
mkdir -p out/pkg-src/share/SuperCollider && cp -a out/sc-prefix/share/SuperCollider/SCClassLibrary out/pkg-src/share/SuperCollider/
cp out/sc-prefix/share/SuperCollider/COPYING out/sc-prefix/share/SuperCollider/AUTHORS out/pkg-src/share/SuperCollider/ 2>/dev/null || true
rm -rf out/sc-prefix

# PipeWire's JACK client library, taken unmodified from the Debian package
docker exec uzume-nb sh -c 'rm -rf /tmp/pj && mkdir /tmp/pj && cd /tmp/pj && apt-get update -qq && apt-get download pipewire-jack >/dev/null && dpkg-deb -x pipewire-jack_*.deb x'
docker cp "uzume-nb:/tmp/pj/x/usr/lib/aarch64-linux-gnu/pipewire-0.3/jack/." out/pkg-src/jack
cp out/pkg-src/jack/libjack.so.0.* out/pkg-src/ && rm -rf out/pkg-src/jack
docker cp uzume-nb:/tmp/pj/x/usr/share/doc/pipewire-jack/copyright out/pkg-src/pipewire-copyright
docker exec uzume-nb rm -rf /tmp/pj
ls out/pkg-src/libjack.so.0.*
