# Uzume

norns scripts on the Cardputer Zero.

Uzume is the norns runtime (matron, crone, softcut and the SuperCollider engine host) built for the Zero. The 128x64 screen is drawn on the 320x170 LCD,
the keyboard stands in for the three keys and three encoders, and sound goes out through PipeWire. Scripts written for norns run as they are.
It comes as one .deb and shows up in the launcher.

What is different from the original is in [docs/DIFFERENCES.md](docs/DIFFERENCES.md).

## Install

```sh
sudo apt install ./uzume_0.1.0_arm64.deb
```

Open Uzume in the launcher. The first start takes about 15 seconds. Scripts go in `~/.local/share/uzume/we/dust/code/<name>/<name>.lua`;
restart Uzume after adding a script that has its own SuperCollider engine. Hold Esc for a second to go back to the launcher.

## Keys

| Keys | |
| --- | --- |
| `1` `2` `3` | K1 K2 K3 (tap `1` for the system menu) |
| `Backspace` / `Enter` | K2 (back) / K3 (ok) |
| `Q` / `W`, PgUp / PgDn | E1 down / up |
| `A` / `S`, `Z` / `C`, left / right | E2 down / up |
| `X` / `F`, down / up, `-` / `=` | E3 down / up |
| `Shift` | encoders move 4 steps at a time |
| hold `Esc` | quit |

Hold an encoder key and it keeps turning. SYSTEM > TINT changes the colour of the screen.

## Build

You need Docker with arm64 images (Debian 13).

```sh
sh third_party/fetch.sh                                                           # upstream sources, at the commits in third_party/PINS
UZUME_BUFFRAMES=2097152 sh norns-port/build.sh                                    # the patched norns binary
docker build --platform linux/arm64 -t uzume-sc -f sc/build/Dockerfile sc/build   # SuperCollider and JACK
sh packaging/collect.sh                                                           # gathers the pieces
sh packaging/build-deb.sh                                                         # out/uzume_0.1.0_arm64.deb
```

The changes to the upstream source are in `norns-port/apply_patches.py` (numbered, with a comment on each) and `norns-port/overlay/`.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). What Uzume is built from, and under which licenses, is listed in [THIRD_PARTY.md](THIRD_PARTY.md).

Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic))
