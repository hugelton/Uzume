#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
# Builds out/uzume_<version>_arm64.deb from out/deploy (norns binary, libs, lua), out/pkg-src (SuperCollider, libjack)
# and this directory. dpkg-deb runs in the arm64 container "uzume-nb".
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$HERE/..
S=$ROOT/out/stage; rm -rf "$S"; mkdir -p "$S"
U=$S/opt/uzume; mkdir -p $U/bin $U/lib $U/etc $U/share/dust-default/uzume-keytest
cp $ROOT/out/deploy/bin/norns $U/bin/; cp $ROOT/out/pkg-src/bin/sclang $ROOT/out/pkg-src/bin/scsynth $U/bin/
cp $HERE/uzume-run $HERE/jack_connect $U/bin/
# libuzume-pageout.so: pushes idle memory into zram (see pageout.c); built in the arm64 container
docker exec uzume-nb mkdir -p /pageout && docker cp $HERE/pageout.c uzume-nb:/pageout/pageout.c && docker exec uzume-nb gcc -O2 -shared -fPIC -o /pageout/libuzume-pageout.so /pageout/pageout.c -lpthread && docker cp uzume-nb:/pageout/libuzume-pageout.so $U/lib/
cp -a $ROOT/out/deploy/lib/libmonome.so.1 $ROOT/out/deploy/lib/libmonome.so.1.4.11 $U/lib/
J=$(ls $ROOT/out/pkg-src/libjack.so.0.* | head -1); cp $J $U/lib/; ln -s $(basename $J) $U/lib/libjack.so.0
cp -a $ROOT/out/pkg-src/lib/SuperCollider $U/lib/
mkdir -p $U/share/norns; cp -a $ROOT/out/deploy/home/we/norns/lua $ROOT/out/deploy/home/we/norns/resources $ROOT/out/deploy/home/we/norns/LICENSE.txt $U/share/norns/
# fonts without a usable licence are not shipped (the font numbers fall back to norns.ttf / tom-thumb, see patch 11)
rm -f $U/share/norns/resources/liquid.ttf $U/share/norns/resources/bmp/creep.bdf
cp $ROOT/out/deploy/home/we/norns/matronrc.lua $U/share/norns/matronrc.lua
mkdir -p $U/share/SuperCollider; cp -a $ROOT/out/pkg-src/share/SuperCollider/. $U/share/SuperCollider/
mkdir -p $U/share/sc; cp -a $ROOT/third_party/norns/sc/core $ROOT/third_party/norns/sc/engines $U/share/sc/
cp $HERE/matronrc.lua $HERE/startup.scd $U/etc/
cp $HERE/keytest.lua $U/share/dust-default/uzume-keytest/uzume-keytest.lua
A=$S/usr/share/APPLaunch; mkdir -p $A/applications $A/share/images
cp $HERE/uzume.desktop $A/applications/; cp $HERE/uzume.png $HERE/uzume_80.png $HERE/uzume_100.png $A/share/images/
C=$S/usr/share/doc/uzume; mkdir -p $C
cp $HERE/copyright $C/copyright; cp $ROOT/LICENSE $C/LICENSE; cp $ROOT/THIRD_PARTY.md $C/THIRD_PARTY.md
cp $ROOT/out/pkg-src/pipewire-copyright $C/libjack-pipewire-copyright
find $S -type f -name '*.gz' -prune -o -type f -print >/dev/null
SIZE=$(du -sk $S | cut -f1); mkdir -p $S/DEBIAN; sed "s/@SIZE@/$SIZE/" $HERE/control.in > $S/DEBIAN/control
chmod 755 $U/bin/*; find $S -type d -exec chmod 755 {} +
docker rm -f uzume-pkg >/dev/null 2>&1 || true
docker run -d --name uzume-pkg --platform linux/arm64 uzume-norns sleep 600 >/dev/null
tar -C $S -cf - . | docker exec -i uzume-pkg sh -c 'mkdir -p /pkg && tar -C /pkg --no-same-owner -xf - && chown -R root:root /pkg && dpkg-deb --root-owner-group --build /pkg /uzume.deb >/dev/null && dpkg-deb -I /uzume.deb | head -3'
mkdir -p $ROOT/out; docker cp uzume-pkg:/uzume.deb $ROOT/out/uzume_0.1.0_arm64.deb; docker rm -f uzume-pkg >/dev/null
ls -l $ROOT/out/uzume_0.1.0_arm64.deb
