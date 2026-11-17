-- gridtest: lights the grid in a diagonal gradient; each press toggles a pad and is shown on screen
-- (uzume grid check; K2 clears, K3 fills)
g = grid.connect()
local on = {}
local last = "press a pad"
local cols, rows = 8, 8

local function paint()
  for x = 1, cols do
    for y = 1, rows do
      local v = on[x .. "," .. y] and 15 or math.floor(((x + y) / (cols + rows)) * 6)
      g:led(x, y, v)
    end
  end
  g:refresh()
end

g.key = function(x, y, z)
  local k = x .. "," .. y
  if z == 1 then on[k] = not on[k] end
  last = string.format("pad %d,%d %s", x, y, z == 1 and "down" or "up")
  print("gridtest key", x, y, z)
  paint()
  redraw()
end

function init()
  print("gridtest: grid cols=" .. tostring(g.cols) .. " rows=" .. tostring(g.rows) .. " name=" .. tostring(g.name))
  paint()
  redraw()
end

function key(n, z)
  if z == 1 and n == 2 then on = {} paint() redraw() end
  if z == 1 and n == 3 then for x = 1, cols do for y = 1, rows do on[x .. "," .. y] = true end end paint() redraw() end
end

function redraw()
  screen.clear()
  screen.level(15)
  screen.move(4, 14)
  screen.text("grid test  " .. tostring(g.cols) .. "x" .. tostring(g.rows))
  screen.move(4, 30)
  screen.text(last)
  screen.move(4, 60)
  screen.level(6)
  screen.text("K2 clear   K3 fill")
  screen.update()
end
