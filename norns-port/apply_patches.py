#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
"""Uzume patches to a pristine norns checkout (run from the norns root). Idempotent. Each edit is small and named."""
import re, shutil, sys, os
root = sys.argv[1] if len(sys.argv) > 1 else '.'
def edit(path, fn):
    p = os.path.join(root, path); s = open(p).read(); n = fn(s)
    if n != s: open(p, 'w').write(n); print('patched', path)

# 1. aarch64: -mfpu is an armv7-only gcc option
edit('norns/wscript', lambda s: s.replace("'-O3', '-Wall', '-mfpu=neon']", "'-O3', '-Wall']")
     .replace("            '-mfpu=neon-fp-armv8',\n            '-mfloat-abi=hard',\n", ""))
# 2. add the Uzume screen backends to the desktop sources
edit('norns/wscript', lambda s: s if 'uzume_screen' in s else s.replace(
     "'../matron/src/hardware/input/sdl.cc',", "'../matron/src/hardware/input/sdl.cc',\n            '../matron/src/hardware/screen/uzume_screen.cc',"))
edit('matron/src/hardware/screen/screens.h', lambda s: s if 'screen_ascii_ops' in s else s.replace(
     "extern screen_ops_t screen_sdl_ops;", "extern screen_ops_t screen_sdl_ops;\nextern screen_ops_t screen_ascii_ops;\nextern screen_ops_t screen_fb_ops;"))
edit('matron/src/hardware/io.cc', lambda s: s if 'screen_ascii_ops' in s else s.replace(
     "    (io_ops_t *)&screen_sdl_ops,", "    (io_ops_t *)&screen_sdl_ops,\n    (io_ops_t *)&screen_ascii_ops,\n    (io_ops_t *)&screen_fb_ops,"))
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overlay/uzume_screen.cc'),
            os.path.join(root, 'matron/src/hardware/screen/uzume_screen.cc'))
print('copied uzume_screen.cc')

# 3. toolchain drift (Debian 13: liblo 0.32, gcc 14)
edit('matron/src/event_types.h', lambda s: s.replace("typedef void *lo_message;", "#include <lo/lo_types.h> // liblo >= 0.32 declares lo_message itself; upstream's `typedef void *lo_message` conflicts"))
edit('crone/src/BufDiskWorker.h', lambda s: s if '#include <string>' in s else s.replace("#include", "#include <string>\n#include", 1))

# 4. libgpiod 2.x has no 1.x API: replace the SSD1322 driver by a no-op stub. sdl.cc uses nested designated
#    initializers that g++ 14 rejects; the SDL backends are not used, drop them.
edit('norns/wscript', lambda s: s.replace("'../matron/src/hardware/screen/ssd1322.cc'", "'../matron/src/hardware/screen/uzume_ssd1322_stub.cc'")
     .replace("            '../matron/src/hardware/screen/sdl.cc',\n", "").replace("            '../matron/src/hardware/input/sdl.cc',\n", ""))
edit('matron/src/hardware/io.cc', lambda s: s.replace("    (io_ops_t *)&screen_sdl_ops,\n", "").replace("    (io_ops_t *)&input_sdl_ops,\n", ""))
edit('matron/src/hardware/screen.cc', lambda s: s)  # (screen.cc itself needs no change: desktop path returns before ssd1322_update)
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overlay/uzume_ssd1322_stub.cc'),
            os.path.join(root, 'matron/src/hardware/screen/uzume_ssd1322_stub.cc'))

# 5. optional: shrink softcut's two static buffers (UZUME_BUFFRAMES=2097152 -> 2 x 8 MB; upstream 2 x 64 MB = 128 MB)
bf = os.environ.get('UZUME_BUFFRAMES')
if bf:
    edit('crone/src/SoftcutClient.h', lambda s: s.replace("BufFrames = 16777216", "BufFrames = %s" % bf))
    edit('lua/core/softcut.lua', lambda s: s.replace("SC.BUFFER_SIZE = 16777216 / 48000", "SC.BUFFER_SIZE = %s / 48000" % bf))

# 6. A USB MIDI device that another client already holds (an ALSA-seq subscription) makes a blocking
#    open() of /dev/snd/midiC*D* wait forever, which froze matron's main thread at start-up on the Zero. Open non-blocking.
edit('matron/src/device/device_midi.cc', lambda s: s.replace('alsa_name, 0) < 0', 'alsa_name, SND_RAWMIDI_NONBLOCK) < 0'))

# 7. keyboard backend 'keys:cpz' (keys 1-3 -> K1-K3; key pairs -> E1-E3 inc/dec), see overlay/uzume_keys.cc
edit('norns/wscript', lambda s: s if 'uzume_keys' in s else s.replace(
     "'../matron/src/hardware/screen/uzume_screen.cc',", "'../matron/src/hardware/screen/uzume_screen.cc',\n            '../matron/src/hardware/input/uzume_keys.cc',"))
edit('matron/src/hardware/input/inputs.h', lambda s: s if 'key_cpz_ops' in s else s.replace(
     "extern input_ops_t key_gpio_ops;", "extern input_ops_t key_gpio_ops;\nextern input_ops_t key_cpz_ops;"))
edit('matron/src/hardware/io.cc', lambda s: s if 'key_cpz_ops' in s else s.replace(
     "    (io_ops_t *)&key_gpio_ops,", "    (io_ops_t *)&key_gpio_ops,\n    (io_ops_t *)&key_cpz_ops,"))
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overlay/uzume_keys.cc'),
            os.path.join(root, 'matron/src/hardware/input/uzume_keys.cc'))

# 8. the Select menu built '/home/we/dust/code/' into every path; use the real code path (HOME is not /home/we on the Zero)
edit('lua/core/menu/select.lua', lambda s: s.replace("table.insert(t,'/home/we/dust/code/' .. filename)", "table.insert(t, paths.code .. filename)"))

# 9. crone's block buffers were sized for JACK periods <= 2048 frames and assert `numFrames < BlockSize`; with PipeWire's
#    quantum forced to 2048 (see docs: no realtime priority on the Zero) that aborts. Allow up to 4096.
edit('crone/src/SoftcutClient.h', lambda s: s.replace("MaxBlockFrames = 2048", "MaxBlockFrames = 4096"))
edit('crone/src/MixerClient.h', lambda s: s.replace("MaxBufFrames = 2048", "MaxBufFrames = 4096"))

# 10. SYSTEM > TINT: colour for the LCD picture. A new menu page (overlay/lua/core/menu/tint.lua), a Lua call
#     `_norns.screen_tint(r, g, b)` (weaver.cc) handled by the screen:fb backend, and the saved tint applied on the first frame.
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overlay/lua/core/menu/tint.lua'),
            os.path.join(root, 'lua/core/menu/tint.lua'))
edit('lua/core/menu.lua', lambda s: s if 'menu/tint' in s else s.replace("m[\"DISPLAY\"] = require 'core/menu/display'", "m[\"DISPLAY\"] = require 'core/menu/display'\nm[\"TINT\"] = require 'core/menu/tint'"))
edit('lua/core/menu/system.lua', lambda s: s if 'TINT' in s else s.replace('list = {"DEVICES > ", "WIFI >", "MODS >", "SETTINGS >", "RESTART", "UPDATE"}', 'list = {"DEVICES > ", "WIFI >", "MODS >", "SETTINGS >", "TINT >", "RESTART", "UPDATE"}').replace('pages = {"DEVICES", "WIFI", "MODS", "SETTINGS", "RESTART", "UPDATE"}', 'pages = {"DEVICES", "WIFI", "MODS", "SETTINGS", "TINT", "RESTART", "UPDATE"}'))
edit('lua/core/screen.lua', lambda s: s if 'apply_saved' in s else s.replace("    module_just_loaded = false\n", "    module_just_loaded = false\n    if _norns.screen_tint then require('core/menu/tint').apply_saved() end\n", 1))
edit('matron/src/weaver.cc', lambda s: s if 'screen_tint' in s else s.replace("static int _screen_gamma(lua_State *l);", "static int _screen_gamma(lua_State *l);\nextern void uz_screen_tint(int r, int g, int b); // uzume_screen.cc\nstatic int _screen_tint(lua_State *l) {\n    lua_check_num_args(3);\n    uz_screen_tint(luaL_checkinteger(l, 1), luaL_checkinteger(l, 2), luaL_checkinteger(l, 3));\n    lua_settop(l, 0);\n    return 0;\n}", 1).replace('    lua_register_norns("screen_gamma", &_screen_gamma);', '    lua_register_norns("screen_gamma", &_screen_gamma);\n    lua_register_norns("screen_tint", &_screen_tint);', 1))

# 11. fonts with no usable licence are not shipped: liquid.ttf is "FontStruct Non-Commercial", bmp/creep.bdf has no licence text.
#     Their font numbers stay valid (scripts select fonts by number) but draw with norns.ttf / tom-thumb instead.
edit('matron/src/hardware/screen.cc', lambda s: s.replace('"liquid.ttf"', '"norns.ttf"').replace('"bmp/creep.bdf"', '"bmp/tom-thumb.bdf"'))

# 12. `require("cjson")`: norns' image has lua-cjson as a Lua module, while matron here carries it built in and only exposes it as the global `_json`.
#     Scripts that `require` it (and carry an arm32 cjson.so of their own, which cannot load on aarch64) work again.
edit('lua/core/config.lua', lambda s: s if 'package.preload["cjson"]' in s else 'package.preload["cjson"] = package.preload["cjson"] or function() return _json end\n' + s)

# 13. ws-wrapper (maiden's REPL bridge) copies the program's output to its own stdout with printf and never flushes: into a log
#     file that arrives in 4 KB blocks, late. uzume-run waits for lines in those logs, so flush each read.
edit('ws-wrapper/src/main.c', lambda s: s if 'fflush(stdout)' in s else s.replace('            printf("%s", buf);\n', '            fwrite(buf, 1, nb, stdout);\n            fflush(stdout);\n', 1))

# 14. SLEEP (home menu) ran `sudo shutdown now`, which would power off the whole device (or fail on the password). On Uzume it
#     saves the state as before and then quits, back to the launcher, the same way as holding Esc (EVENT_QUIT). New Lua call `_norns.quit()`.
edit('matron/src/weaver.cc', lambda s: s if '_uzume_quit' in s else s.replace("static int _system_action(lua_State *l);", "static int _system_action(lua_State *l);\nstatic int _uzume_quit(lua_State *l) {\n    (void)l;\n    event_post(event_data_new(EVENT_QUIT));\n    return 0;\n}", 1).replace('    lua_register_norns("screen_gamma", &_screen_gamma);', '    lua_register_norns("screen_gamma", &_screen_gamma);\n    lua_register_norns("quit", &_uzume_quit);', 1))
edit('lua/core/norns.lua', lambda s: s.replace('  _norns.system_action("shutdown", report_launch_failure("shutdown"))\n', '  _norns.quit() -- Uzume: back to the launcher instead of powering off\n', 1))
