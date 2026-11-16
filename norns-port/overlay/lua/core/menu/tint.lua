-- SYSTEM > TINT: colour of the LCD picture (Uzume). The norns screen is 16 greys; on a full-colour LCD they can be any colour ramp.
-- SPDX-License-Identifier: GPL-3.0-or-later
local m = {
  pos = 1,
}

local presets = {
  {"WHITE",  255, 255, 255},
  {"PAPER",  255, 240, 205},
  {"AMBER",  255, 176,   0},
  {"GREEN",   60, 255, 110},
  {"CYAN",    80, 230, 255},
  {"BLUE",   120, 160, 255},
  {"PINK",   255, 120, 200},
  {"RED",    255,  70,  70},
}

local file = function() return _path.data .. 'system.tint' end

local function apply(i)
  local p = presets[i] or presets[1]
  if _norns.screen_tint then _norns.screen_tint(p[2], p[3], p[4]) end
end

local function load_saved()
  local f = loadfile(file())
  local s = f and f() or {}
  for i, p in ipairs(presets) do
    if p[1] == s.tint then return i end
  end
  return 1
end

local function save(i)
  local f = io.open(file(), "w")
  if f then f:write('return {tint="' .. presets[i][1] .. '"}'); f:close() end
end

-- called once by screen.lua when the first frame is drawn
function m.apply_saved()
  apply(load_saved())
end

m.key = function(n, z)
  if n == 2 and z == 1 then
    _menu.set_page("SYSTEM")
  end
end

m.enc = function(n, delta)
  if n == 2 or n == 3 then
    local i = util.clamp(m.pos + delta, 1, #presets)
    if i ~= m.pos then
      m.pos = i
      apply(i)
      save(i)
      _menu.redraw()
    end
  end
end

m.redraw = function()
  screen.clear()
  screen.level(15)
  screen.move(0, 8)
  screen.text("TINT")
  for i = 1, 6 do
    local k = m.pos + i - 3
    if presets[k] then
      screen.level(i == 3 and 15 or 4)
      screen.move(0, 10 * i + 8)
      screen.text(presets[k][1])
    end
  end
  -- a grey ramp, to see the colour
  for g = 0, 15 do
    screen.level(g)
    screen.rect(80 + (g % 4) * 12, 12 + math.floor(g / 4) * 12, 11, 11)
    screen.fill()
  end
  screen.update()
end

m.init = function() m.pos = load_saved() end
m.deinit = norns.none

return m
