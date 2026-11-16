# Licenses and third-party code

Uzume's own code is GPL-3.0-or-later (`LICENSE`). It is built on the norns sources, which are GPL-3.0, so the combined work is GPL-3.0-or-later. Upstream copyright notices and license texts stay in place.

## Built into Uzume

| Component | License | Source |
| --- | --- | --- |
| norns (`matron`, `crone`, Lua core, SuperCollider classes), Copyright (c) monome and norns contributors | GPL-3.0-or-later | https://github.com/monome/norns, commit in `third_party/PINS` |
| softcut-lib (inside norns) | GPL-3.0 | part of the norns source |
| libmonome | ISC | https://github.com/monome/libmonome, commit in `third_party/PINS` |
| Ableton Link (inside norns) | GPL-2.0-or-later | part of the norns source |
| doctest, lua-cjson | MIT | part of the norns source |
| readerwriterqueue, concurrentqueue | BSD-2-Clause (with zlib-licensed semaphores) | part of the norns source |
| trompeloeil | BSL-1.0 | part of the norns source |

The Uzume changes to the norns sources are in `norns-port/` (`apply_patches.py` and `overlay/`); the upstream commit plus these files are the corresponding source of the `norns` binary.

## Shipped as separate programs

| Component | License | Source |
| --- | --- | --- |
| SuperCollider 3.13 (`scsynth`, `sclang`, class library, UGens) | GPL-3.0 | Debian source package `supercollider`; build recipe `sc/build/Dockerfile`; license in `sc/lic/` |
| PipeWire's JACK client library (`libjack`), unmodified | MIT and LGPL-2.1-or-later | Debian package `pipewire-jack`; its copyright file is installed as `libjack-pipewire-copyright` |

The `libjack.so.0` in the package can be replaced by any other JACK client library.

## maiden (web editor, off unless turned on)

| Component | License | Source |
| --- | --- | --- |
| maiden (`maiden` server and the web app in `share/maiden/app`), Copyright (c) monome and maiden contributors | GPL-3.0 | https://github.com/monome/maiden, commit 2e64f7d; build recipe and the one change (a `--host` option) in `packaging/maiden/build-maiden.sh` |
| `ws-wrapper` (connects maiden's consoles to norns and SuperCollider) | GPL-3.0-or-later | part of the norns source (patch 13 in `norns-port/apply_patches.py`) |
| Go modules compiled into `maiden` (gin, cobra, viper, go-git, golang.org/x/..., the Go standard library and others) | MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, MPL-2.0 (hcl) | listed with their license texts in `/usr/share/doc/uzume/maiden-third-party-licenses.txt` |
| npm packages bundled into the web app (React, Redux, Ace, lodash, core-js and others) | MIT, BSD-3-Clause, ISC, Apache-2.0 | listed with their license texts in the same file |

## Fonts installed in `/opt/uzume/share/norns/resources`

| Font | License |
| --- | --- |
| Roboto | Apache-2.0 |
| Bitstream Vera | Bitstream Vera license |
| Particle | SIL OFL 1.1 |
| basis33 | MIT |
| norns.ttf | part of the norns source |
| bmp/ctrld-fixed-* | MIT |
| bmp/scientifica-*, bmp/ter-u* (Terminus) | SIL OFL |
| bmp/tom-thumb.bdf | CC-BY-4.0 |
| bmp/unscii-* | public domain |

## System libraries

Linked dynamically from Debian, not bundled: `liblo`, `libsndfile`, `libasound`, `libavahi`, `libcairo`, `liblua5.3`, `libnng`, `libmbedtls`, `libevdev`, `libudev`, `libreadline`, `libfftw3`, `libboost`. Their licenses are in `/usr/share/doc/<package>/copyright`.
