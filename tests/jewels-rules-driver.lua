-- jewels-rules-driver.lua: plays the original Lua game's own main.lua and board.lua through their pad input path
-- (the JEWELS_DRIVER hook) with every animation completing at once, and prints each step as one JSON line, for
-- tests/jewels-rules.mjs to compare with the C port.
--
--   lua tests/jewels-rules-driver.lua <the original's app directory> <seed> <steps>
--
-- What is stubbed and why:
--   * love.math.random: a small xorshift32 generator (the same one games/jewels/src/rules.c uses), so both sides
--     draw the same numbers from the same seed.
--   * pairs(): tables keyed "x,y" (the matched-cell sets) are walked in row order (y, then x). Lua leaves the order
--     of pairs() unspecified; the rules only depend on it where a new board rerolls its matched cells, and the
--     C port walks rows in that order.
--   * the animation helper: a tween lands at once and calls its callback, so a move, its cascade and any reshuffle
--     resolve inside the frame that pressed the button.
--   * drawing, sound and particles: no-ops (the particles' own random draws are presentation, not rules; the C
--     game gives its effects a separate generator).
-- The board, scoring, cascade, hint, reshuffle and input handling are the original's code, unchanged.

local appdir, seed, steps = arg[1], tonumber(arg[2]), tonumber(arg[3])
assert(appdir and seed and steps, "usage: lua jewels-rules-driver.lua <app dir> <seed> <steps>")
package.path = appdir .. "/?.lua;" .. appdir .. "/?/init.lua;" .. package.path

-- the generator: xorshift32, never zero
local state = 0
local function seedrng(s)
  state = math.tointeger(s) & 0xffffffff
  if state == 0 then state = 0x9e3779b9 end
end
local function nextu32()
  local x = state
  x = x ~ ((x << 13) & 0xffffffff)
  x = x ~ (x >> 17)
  x = x ~ ((x << 5) & 0xffffffff)
  state = x
  return x
end
local function random(a, b)
  if a == nil then return nextu32() / 4294967296.0 end
  if b == nil then return (nextu32() % a) + 1 end
  return a + (nextu32() % (b - a + 1))
end

-- row-order pairs for the "x,y" sets
local rawpairs = pairs
pairs = function(t)
  local keys, allxy = {}, true
  for k in rawpairs(t) do
    if type(k) ~= "string" or not k:match("^%d+,%d+$") then allxy = false; break end
    keys[#keys + 1] = k
  end
  if not allxy or #keys == 0 then return rawpairs(t) end
  table.sort(keys, function(a, b)
    local ax, ay = a:match("^(%d+),(%d+)$")
    local bx, by = b:match("^(%d+),(%d+)$")
    ax, ay, bx, by = tonumber(ax), tonumber(ay), tonumber(bx), tonumber(by)
    if ay ~= by then return ay < by end
    return ax < bx
  end)
  local i = 0
  return function()
    i = i + 1
    local k = keys[i]
    if k ~= nil then return k, t[k] end
  end
end

local noop = function() end
local fontStub = setmetatable({}, { __index = function() return noop end })
love = {
  math = { random = random, setRandomSeed = seedrng },
  pad = { isDown = function() return false end },
  graphics = setmetatable({}, { __index = function() return noop end }),
  audio = { newSource = function() error("no audio") end },
}
wc = { entropy = function() return seed end }

package.preload["lib.theme"] = function()
  return setmetatable({ gold = { 1, 0.8, 0.3 }, quiet = { 0.6, 0.6, 0.6 }, white = { 1, 1, 1 },
                        fontSmall = 28, fontMid = 36, fontBig = 48, fontHuge = 96 }, { __index = function() return 0 end })
end
package.preload["lib.ui"] = function() return { font = function() return fontStub end, button = noop } end
package.preload["lib.sounds"] = function() return { loadAll = noop, play = noop } end
package.preload["lib.anim"] = function()
  return {
    tween = function(obj, to, dur, onDone)
      for k, v in rawpairs(to) do obj[k] = v end
      if onDone then onDone() end
    end,
    update = noop, busy = function() return false end,
  }
end
package.preload["jewels"] = function() return { bake = noop, draw = noop, color = function() return { 1, 1, 1 } end } end
package.preload["fx"] = function()
  return { init = noop, initPops = noop, burst = noop, popup = noop, update = noop, draw = noop, drawPops = noop,
           count = function() return 0 end, busy = function() return false end }
end

-- the button the driver presses on the next frame
local pending = nil
JEWELS_DRIVER = function() local b = pending; pending = nil; return b end

dofile(appdir .. "/main.lua")
love.load()
local S = JEWELS_STATE
local B = require("board")

-- the game's private state, read through the upvalues of love.update and the functions it reaches
local function upvalue(name)
  local seen = {}
  local function find(f)
    if seen[f] then return nil, false end
    seen[f] = true
    local i = 1
    while true do
      local n, v = debug.getupvalue(f, i)
      if not n then return nil, false end
      if n == name then return v, true end
      if type(v) == "function" then
        local r, ok = find(v)
        if ok then return r, true end
      end
      i = i + 1
    end
  end
  return (find(love.update))
end

local function gridString(g)
  local t = {}
  for y = 1, B.H do for x = 1, B.W do t[#t + 1] = tostring(g[y][x]) end end
  return table.concat(t)
end

local step = 0
local function press(b)
  pending = b
  love.update(1 / 60)
  step = step + 1
  local sel, hint = upvalue("sel"), upvalue("hintMove")
  local flash = upvalue("reshuffleFlash")
  io.write(string.format(
    '{"step":%d,"button":"%s","grid":"%s","score":%d,"best":%d,"moves":%d,"cur":[%d,%d],"sel":%s,"hint":%s,"state":"%s","reshuffled":%s}\n',
    step, b, gridString(S.grid), S.score, upvalue("best"), S.moves, S.cur.x, S.cur.y,
    sel and string.format("[%d,%d]", sel.x, sel.y) or "null",
    hint and string.format("[%d,%d,%d,%d]", hint.x, hint.y, hint.dx, hint.dy) or "null",
    S.state, (flash and flash >= 1.0) and "true" or "false"))
end

io.write(string.format('{"step":0,"button":"","grid":"%s","score":0,"best":0,"moves":0,"cur":[%d,%d],"sel":null,"hint":null,"state":"%s","reshuffled":false}\n',
  gridString(S.grid), S.cur.x, S.cur.y, S.state))

-- walk the cursor to (x, y)
local function walk(x, y)
  while S.cur.x < x do press("right") end
  while S.cur.x > x do press("left") end
  while S.cur.y < y do press("down") end
  while S.cur.y > y do press("up") end
end
local dirs = { [1] = "right", [-1] = "left" }

-- Deadlocked boards are rare in play, so the reshuffle is also asked for directly: every board of the form
-- ((a*x + b*y) mod 6) + 1 that has no match and no legal move is reshuffled from the generator's current state.
for a = 1, 5 do
  for b = 1, 5 do
    local g = {}
    for y = 1, B.H do
      g[y] = {}
      for x = 1, B.W do g[y][x] = ((a * x + b * y) % 6) + 1 end
    end
    if not B.anyMatch(g) and #B.legalMoves(g) == 0 then
      local before = gridString(g)
      local kept = B.reshuffle(g, B.KINDS)
      io.write(string.format('{"reshuffle":"%s","after":"%s","kept":%s}\n', before, gridString(g), kept and "true" or "false"))
    end
  end
end

-- the policy: a function of the visible state only, so the C side is told nothing but the buttons
local k = 0
while step < steps do
  k = k + 1
  local ms = B.legalMoves(S.grid)
  local r = k % 10
  if r == 3 then
    press((k % 20 == 3) and "x" or "y")                  -- the hint, on either of its buttons
  elseif r == 6 then
    -- an illegal swap: the first pair in row order that does not match, picked up and pushed
    local done = false
    for y = 1, B.H do
      for x = 1, B.W - 1 do
        if not done and not B.isLegal(S.grid, x, y, x + 1, y) then
          walk(x, y); press("a"); press("right"); done = true
        end
      end
    end
  elseif r == 8 then
    press("b"); press("b")                               -- pick up and put down
  elseif r == 9 then
    -- push off the board's edge with a jewel held (nothing happens), then put it down
    walk(1, S.cur.y); press("a"); press("left"); press("a")
  elseif #ms > 0 then
    local m = ms[((k * 7 + S.score // 10) % #ms) + 1]
    walk(m.x, m.y)
    press((k % 2 == 0) and "a" or "b")
    if m.dx ~= 0 then press(dirs[m.dx]) else press(m.dy > 0 and "down" or "up") end
  end
end
