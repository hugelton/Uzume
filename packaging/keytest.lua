-- uzume keytest: shows K1-K3 and E1-E3 as they arrive from the Cardputer Zero keyboard
-- keys 1 2 3 = K1 K2 K3; Q/W, A/S (Z/C), X/F = E1, E2, E3 down/up; Backspace = K2, Enter = K3, hold Esc to quit
-- SPDX-License-Identifier: GPL-3.0-or-later
local k = {0, 0, 0}
local e = {0, 0, 0}
local last = "press 1 2 3 / Q W / A S / X F"
function key(n, z) k[n] = z; last = "K" .. n .. (z == 1 and " down" or " up"); redraw() end
function enc(n, d) e[n] = e[n] + d; last = "E" .. n .. " " .. (d > 0 and "+" or "") .. d; redraw() end
function redraw()
  screen.clear()
  screen.level(15); screen.move(2, 10); screen.text("uzume key test")
  screen.level(8); screen.move(2, 24); screen.text("K " .. k[1] .. k[2] .. k[3])
  for i = 1, 3 do screen.move(2 + (i - 1) * 42, 38); screen.text("E" .. i .. "=" .. e[i]) end
  screen.level(15); screen.move(2, 56); screen.text(last)
  screen.update()
end
