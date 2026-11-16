# Differences from the original norns

Uzume uses the original sources: matron, crone, softcut, the Lua core and the SuperCollider engine host come from the upstream commit in `third_party/PINS`.
The changes are in `norns-port/` (patches 1 to 11 in `apply_patches.py`, new files in `overlay/`). Everything else on this page is a consequence of the hardware.

## Hardware

| | norns | Uzume |
| --- | --- | --- |
| Screen | SSD1322 OLED, 128x64, 16 greys | the same picture on the 320x170 LCD (`/dev/fb0`): 2x (256x128, centred) or 2.5x (320x160, smoothed) |
| Screen colour | grey | SYSTEM > TINT: eight colours (patch 10) |
| Brightness, contrast, gamma (SYSTEM > SETTINGS > DISPLAY) | change the OLED | the page is there, it does nothing |
| Keys and encoders | 3 keys, 3 encoders | the keyboard, see the README; no encoder acceleration |
| USB keyboards | menu shortcuts | untouched, but the built-in keyboard is read by Uzume itself |
| Power, battery, sleep, GPIO | norns shield | not handled |
| Wi-Fi, update, reset pages | image with NetworkManager and systemd | the pages are there; the Zero's own OS does that, so they do not work |
| MIDI | ALSA raw MIDI | the same. Patch 6: a device that another program already holds no longer blocks the start |
| Grid, arc | through serialosc | libmonome is built, serialosc is not included |

## Audio

| | norns | Uzume |
| --- | --- | --- |
| JACK | jackd | PipeWire's libjack, no jackd |
| Block size | JACK period 256 to 1024 | PipeWire's quantum is set to 2048 frames (43 ms) while Uzume runs. At 1024 the graph missed deadlines because user processes have no realtime priority. Latency is higher than on norns |
| crone buffers | up to 2048 frames | up to 4096 (patch 9) |
| softcut buffers | 2 x 64 MB, about 350 s each | 2 x 8 MB, about 43 s each, because the Zero has 352 MB of RAM |
| Inputs | codec on the shield | the Zero's input and USB audio, as PipeWire offers them. The TRRS input is not set up |
| Sample rate | 48 kHz | 48 kHz |

## SuperCollider

| | norns | Uzume |
| --- | --- | --- |
| Server size | scsynth defaults | small: memSize 2048, 256 buffers, 512 nodes, 16 random generators, 2 in and 2 out |
| Start | all at boot | sclang first (its start peaks near 95 MB), then norns. The start takes about 15 seconds |
| Engines | stock engines and the ones scripts bring | the same. Engines are read when Uzume starts, so a new one needs a restart |
| sc3-plugins | optional | not included; engines that need them do not load |
| Voices | | keep them low: the server is small and the cores run at 1 GHz |

## System

| | norns | Uzume |
| --- | --- | --- |
| Home | `/home/we` | `~/.local/share/uzume/we` (`dust/code`, `dust/audio`, `dust/data`); the program is in `/opt/uzume` |
| Paths in the Select menu | `/home/we/dust/code`, fixed | the real path (patch 8) |
| maiden (web editor, script catalogue) | yes | not included. Copy scripts into `dust/code` |
| Fonts | the full set | the same set, except `liquid.ttf` and `bmp/creep.bdf`, which have no usable license. Those font numbers draw with norns.ttf and tom-thumb (patch 11) |
| Services | systemd units | one launcher script (`/opt/uzume/bin/uzume-run`) and a `.desktop` entry. While Uzume runs, the desktop panel, file manager, portal and volume helpers and the PulseAudio bridge are stopped (about 10 MB more free RAM) and started again at the end; `UZUME_KIOSK=0` turns this off |
| `require("cjson")` | the image has it as a Lua module | works (patch 12): the copy built into matron is exposed under that name |
| Link, MIDI clock, softcut, params, metro, clock, osc | yes | yes, it is the same code |

## Memory

The launcher script pushes memory that nothing is using into zram (`libuzume-pageout.so`, preloaded into norns, sclang and scsynth; `UZUME_PAGEOUT=0` turns it off)
and stops the desktop's helpers while Uzume runs (`UZUME_KIOSK=0` turns that off). Together they leave about 140 MB free while a script runs, instead of about 60 MB.

## Limits

- Scripts that write `/home/we/...` paths by hand do not find them: Uzume's home is `~/.local/share/uzume/we`.
- About 40 MB of RAM is free with the desktop running and no memory tricks, and swap is in use. A script with big buffers or many voices can stall.
- The LCD shows the launcher until norns draws its first frame.
- Latency is about 43 ms plus whatever the engine adds.
