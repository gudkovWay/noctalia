--!nonstrict
-- Continuous Frame02 bar. Native hover motion reveals metadata without moving
-- the fixed cover; audio/metadata refreshes preserve the current animation.
local status = require("./shared/status.luau")
local actions = require("./shared/actions.luau")
local skin = require("./desktop_skin.luau")
local spectrum = require("./shared/music-spectrum.luau").new()
local KEY = "skin.status"
local ASSET = "skin-assets/joinery-bar/"
local envelope = status.envelope(nil)
local width, height, startWidth, endWidth = 2560, 64, 0, 260
local lastTrack, lastDrawAt = nil, 0

local function playing()
  local music = envelope.music
  return skin.kumihimo() and envelope.ok and music.present and music.linked and music.playing
end
local function image(key, path, x, y, w, h, opacity)
  return ui.image({key = key, path = path, x = x, y = y,
    width = w, height = h, fit = "stretch", opacity = opacity or 1})
end
local function repeated(key, path, x, y, w, h, period, opacity)
  local tiles = {}
  for offset = 0, math.max(0, w - 1), period do
    table.insert(tiles, image(key .. tostring(offset), path, offset, 0, period, h, opacity))
  end
  return ui.box({key = key, x = x, y = y, width = w, height = h, fill = "#00000000", clip = true}, tiles)
end
local function draw()
  if not skin.kumihimo() or barWidget.isVertical() then
    barWidget.render(ui.box({key = "joinery-bar", width = 0, height = 0, visible = false}))
    return
  end
  local music = envelope.music
  local title = not envelope.ok and "Music unavailable" or not music.present and "No player"
    or not music.linked and "Player disconnected" or music.title or "Unknown track"
  local artist = music.artist or ""
  local left = math.max(math.min(640, math.max(320, width * 0.35)),
    math.min(width - math.max(112, endWidth), startWidth + 16))
  local fittingX = width - 64
  local right = math.max(left, width - math.max(112, endWidth))
  local center = math.max(left + 60, width / 2)
  local coverX = center - 20
  local revealX = coverX + 40
  local peak = 0
  for _, value in ipairs(spectrum.far) do peak = math.max(peak, value) end
  local active = playing() and peak > 0.015
  local extension = math.clamp(width - endWidth - revealX - 39, 0, 220)
  local nodes = {
    repeated("wood-board", ASSET .. "board.png", 8, 12, left - 8, 40, 256),
    repeated("rain-glass", ASSET .. "glass-repeat.png", left, 12, right - left, 40, 526, 0.85),
    repeated("tray-wood-bay", ASSET .. "board.png", right, 12, math.max(0, fittingX - right), 40, 256),
    ui.graph({key = "mist", x = left, y = 30, width = right - left, height = 22,
      values = spectrum.far, values2 = spectrum.near, color = "#819FB2B0", color2 = "#49697AA0",
      lineWidth = 0.4, fillOpacity = 1, visible = active}),
    repeated("top-rail", ASSET .. "rail-top.png", 8, 4, width - 16, 8, 256),
    repeated("bottom-rail", ASSET .. "rail-bottom.png", 8, 52, width - 16, 8, 256),
    image("left-shoulder", ASSET .. "shoulder-left.png", left - 12, 12, 26, 40),
    image("right-shoulder", ASSET .. "shoulder-right.png", right - 14, 12, 26, 40),
    image("left-fitting", ASSET .. "left-end.png", 8, 4, 64, 56),
    image("right-fitting", ASSET .. "right-end.png", fittingX, 4, 56, 56),
    image("fixed-grip", "skin-assets/rain-music/grip.png", coverX - 27, 4, 27, 56),
  }
  local cover
  if music.present and music.linked and music.artPath and music.artPath ~= "" then
    cover = ui.image({key = "track-cover", path = music.artPath, width = 40, height = 40, fit = "cover"})
  else
    cover = ui.glyph({key = "empty-cover", x = 10, y = 10, name = "music", size = 20, color = "#C7D4D9"})
  end
  table.insert(nodes, ui.box({key = "cover", x = coverX, y = 12, width = 40, height = 40,
    fill = "#172830"}, {cover}))
  table.insert(nodes, ui.slidingReveal({key = "music-reveal", x = revealX, y = 4,
    width = extension + 27, height = 56, handleWidth = 27, handlePath = ASSET .. "grip-right.png",
    hoverBefore = 67, coverWidth = 40, onClick = "onClick"}, {
    ui.box({key = "metadata-backing", x = 0, y = 8, width = extension, height = 40,
      fill = "#211712", clip = true}, {
      image("metadata-wood", ASSET .. "board.png", 0, 0, 256, 40),
    }),
    ui.label({key = "track-title", x = 12, y = 9, text = title, fontSize = 13,
      fontFamily = "Noto Sans", fontWeight = "normal", color = "#EBE9E1", maxWidth = 198, maxLines = 1}),
    ui.label({key = "track-artist", x = 12, y = 27, text = artist, fontSize = 11,
      fontFamily = "Noto Sans", fontWeight = "normal", color = "#ADBBC0", maxWidth = 198, maxLines = 1}),
  }))
  barWidget.render(ui.box({key = "joinery-bar", width = width, height = height,
    fill = "#00000000", visible = true, clip = true}, nodes))
end

local function refresh(value)
  envelope = status.envelope(value)
  local music = envelope.music
  local track = tostring(music.title or "") .. "|" .. tostring(music.artist or "")
  if track ~= lastTrack or not playing() then spectrum:clear() end
  lastTrack = track
  spectrum:step(noctalia.nowMs(), playing())
  draw()
end
function onBarGeometry(w, h, start, ending)
  width, height = math.max(1, w), math.max(1, h)
  startWidth, endWidth = math.max(0, start), math.max(0, ending)
  draw()
end
function onAudioSpectrum(valuesCsv, stateCsv)
  local now = noctalia.nowMs()
  spectrum:accept(valuesCsv, now, playing())
  if now - lastDrawAt < 50 then return end
  lastDrawAt = now
  spectrum:step(now, playing())
  draw()
end
function update()
  spectrum:step(noctalia.nowMs(), playing())
  draw()
end
function onClick() actions.openPanel(actions.PANEL.tmus) end
function onMiddleClick() actions.tmus("toggle") end
function onRightClick() actions.openPanel(actions.PANEL.chooser) end
function onScroll(axis, steps)
  local count = tonumber(steps) or 0
  if axis == "vertical" and count ~= 0 then
    actions.tmus("vol-by", (count < 0 and "+" or "-") .. tostring(math.abs(count) * 5))
  end
end
noctalia.setUpdateInterval(250)
noctalia.state.watch(KEY, refresh)
refresh(noctalia.state.get(KEY))
