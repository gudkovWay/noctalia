--!nonstrict
-- Passive bar entry. Stagecraft keeps its overview icon; Kumihimo shows real
-- monitoring and AI/clock on the shared Frame02 board drawn by music-preview.
-- Left: summary / AI. Middle: dashboard. Right: chooser. Wheel: music volume.
local material = require("./shared/material.luau")
local status = require("./shared/status.luau")
local actions = require("./shared/actions.luau")
-- Receipt-inactive families keep the ordinary Stagecraft path.
local sharedSkin = require("./desktop_skin.luau")
local metrics = require("./shared/bar-metrics.luau")
local METRIC_MARKS = {
  { icon = "cpu", label = "CPU usage" },
  { icon = "temperature", label = "CPU temperature" },
  { icon = "cpu-2", label = "RAM used / total" },
  { icon = "device-desktop", label = "GPU usage" },
  { icon = "temperature", label = "GPU temperature" },
  { icon = "database", label = "Disk / usage" },
}

local KEY = "skin.status"
local ART = 16
-- Height of the accepted Stagecraft bar; the host capsule owns padding and
-- radius, so the entry draws an icon only and never its own plate.
local BAR_H = 24
local SCROLL_STEP = 5

-- Host audio spectrum (same contract as the original q/tmus widget): bands
-- arrive as a CSV string, republished on the lightweight `tmus.spectrum` state
-- key for the panel. Throttled to the established 125 ms cap (8 Hz) — the
-- panel's full redraw ceiling; writing more often is pointless. Full snapshot
-- KEY "skin.status" is never touched and no bar redraw is triggered.
local SPECTRUM_MS = 125
local lastSpectrumAt = 0

local envelope = status.envelope(nil)
local signature = nil

local function systemStats()
  local stats = noctalia.systemStats()
  if type(stats) ~= "table" then
    return nil
  end
  return stats
end

-- Explicit recipe styling: every colour comes from the six recipe tokens, so
-- the entry never inherits a host palette role.
local function entryGlyph(tokens, music, stats)
  if music.playing then
    return "music", tokens.colors.accent
  end
  if music.paused then
    return "player-pause", tokens.colors.muted
  end
  if music.present and music.linked then
    return "music", tokens.colors.text
  end
  if type(stats) == "table" and stats.cpu then
    return "gauge", tokens.colors.muted
  end
  return "help-circle", tokens.colors.muted
end

local function artNode(music)
  if not music.present then
    return nil
  end
  local art = music.artPath
  if art and art ~= "" and (music.playing or music.paused) then
    return ui.image({
      key = "art",
      path = art,
      width = ART,
      height = ART,
      radius = ART // 2,
      fit = "cover",
      opacity = music.paused and 0.6 or 1.0,
    })
  end
  return nil
end

local function tooltipRows(music, ai, stats)
  local rows = {}
  table.insert(rows, { key = "Music", value = status.musicState(music) })
  if music.title then
    table.insert(rows, { key = "Track", value = music.title })
  end
  if music.artist then
    table.insert(rows, { key = "Artist", value = music.artist })
  end
  if music.duration then
    table.insert(rows, { key = "Time", value = status.clock(music.position) .. " / " .. status.clock(music.duration) })
  elseif music.position then
    table.insert(rows, { key = "Time", value = status.clock(music.position) })
  end
  if music.volume then
    table.insert(rows, { key = "Music volume", value = status.percent(music.volume) or "Unknown" })
  end
  local modes = {}
  if music.shuffle then
    table.insert(modes, "shuffle")
  end
  if music.loop and music.loop ~= "none" then
    table.insert(modes, "repeat: " .. music.loop)
  end
  if #modes > 0 then
    table.insert(rows, { key = "Mode", value = table.concat(modes, " · ") })
  end

  if ai.present then
    if ai.unread then
      table.insert(rows, { key = "AI unread", value = tostring(ai.unread) })
    elseif ai.total then
      table.insert(rows, { key = "AI index", value = tostring(ai.total) })
    end
    local provider = ai.providers and ai.providers[1]
    if provider then
      table.insert(rows, { key = provider.title or "AI", value = status.providerLine(provider) })
    end
  end

  if type(stats) == "table" then
    if type(stats.cpu) == "table" and stats.cpu.usagePercent then
      table.insert(rows, { key = "CPU", value = status.percent(stats.cpu.usagePercent) })
    end
    if type(stats.ram) == "table" and stats.ram.usagePercent then
      table.insert(rows, { key = "RAM", value = status.percent(stats.ram.usagePercent) })
    end
  end

  if not envelope.ok then
    table.insert(rows, { key = "Bridge", value = envelope.error or "Unknown" })
  elseif envelope.at then
    table.insert(rows, { key = "Bridge", value = "ok · " .. (status.age(envelope.at) or "Unknown") })
  end
  return rows
end


local function kumihimoDraw()
  local values = metrics.readouts(systemStats(), noctalia.diskStats("/"))
  local nextSignature = table.concat(values, "|")
  if nextSignature == signature then return end
  signature = nextSignature
  local columns = {{}, {}, {}}
  for i, text in ipairs(values) do
    local col = (i - 1) % 3 + 1
    local value = text:match("^%S+%s+(.+)$") or text
    values[i] = value
    if i == 3 then value = value:gsub("/[%d.]+", "") end
    table.insert(columns[col], ui.row({
      key = "metric-cell-" .. tostring(i), height = 20, gap = 6, align = "start",
    }, {
      ui.glyph({
        key = "metric-icon-" .. tostring(i), name = METRIC_MARKS[i].icon,
        size = 16, color = "#D6AE70",
      }),
      ui.label({
        key = "metric-" .. tostring(i), text = value,
        fontSize = 14, fontFamily = "ComicShannsMono Nerd Font", fontWeight = "bold",
        maxLines = 1, color = "#FFF1D6",
      }),
    }))
  end
  local readouts = {}
  for col, cells in ipairs(columns) do
    table.insert(readouts, ui.column({
      key = "metric-column-" .. tostring(col), width = col < 3 and 88 or nil,
      height = 40, gap = 0, align = "start",
    }, cells))
  end
  -- Let native font metrics size the final column, not an empty 144px reserve.
  -- The grips follow the content; the inner wood has the same inset at both ends.
  barWidget.render(ui.row({
    key = "kumihimo-root", height = 56, gap = 0, align = "start",
  }, {
    ui.spacer({ key = "left-fitting-space", width = 72, height = 56 }),
    ui.row({ key = "metrics", height = 56, gap = 0, onClick = "openControlTag" }, {
      ui.image({ key = "metrics-grip-left", width = 27, height = 56,
        path = "skin-assets/rain-music/grip.png", fit = "stretch" }),
      ui.row({ key = "metrics-content", height = 56, paddingH = 6, paddingV = 8,
        gap = 0, align = "start" }, readouts),
      ui.image({ key = "metrics-grip-right", width = 27, height = 56,
        path = "skin-assets/joinery-bar/grip-right.png", fit = "stretch" }),
    }),
  }))
  local rows = {}
  for i, value in ipairs(values) do
    table.insert(rows, { key = METRIC_MARKS[i].label, value = value })
  end
  barWidget.setTooltip(rows)
end

local function draw()
  if sharedSkin.kumihimo() then
    kumihimoDraw()
    return
  end

  local tokens = material.tokens()
  local music = envelope.music
  local stats = systemStats()
  local glyphName, glyphColor = entryGlyph(tokens, music, stats)

  local nextSignature = table.concat({
    tokens.family, tokens.density, tokens.topology,
    tostring(envelope.ok), music.status or "", music.title or "", music.artist or "",
    music.artPath or "", tostring(music.paused),
  }, "|")
  if nextSignature == signature then
    return
  end
  signature = nextSignature

  -- Icon only: the overview lives in the tooltip and the status rail.
  local art = artNode(music)
  local children
  if art then
    children = { art }
  else
    children = { ui.glyph({ key = "glyph", name = glyphName, size = ART, color = glyphColor }) }
  end

  local container = barWidget.isVertical() and ui.column or ui.row
  barWidget.render(container({ key = "skin-widget", gap = 6, align = "center", height = BAR_H }, children))
  barWidget.setTooltip(tooltipRows(music, envelope.ai, stats))
end

local function refresh(value)
  envelope = status.envelope(value)
  signature = nil
  draw()
end



function openControlTag()
  actions.openPanel(actions.PANEL.summary)
end

function onClick()
  actions.openPanel(actions.PANEL.summary)
end

function onMiddleClick()
  actions.openPanel(actions.PANEL.dashboard)
end

-- Host audio bridge: forward the real 24-band CSV to the shared spectrum key,
-- only while Kumihimo is active (Stagecraft leaves the key to its own
-- producer). An empty CSV from the host is forwarded as-is — the host clear
-- contract must not leave stale band values behind. Lightweight write only:
-- no snapshot of KEY, no draw, no synthetic values.
function onAudioSpectrum(valuesCsv, stateCsv)
  local now = noctalia.nowMs()
  if now - lastSpectrumAt < SPECTRUM_MS then
    return
  end
  lastSpectrumAt = now
  if not sharedSkin.kumihimo() then
    return
  end
  noctalia.state.set("tmus.spectrum", tostring(valuesCsv or ""))
end

function onRightClick()
  actions.openPanel(actions.PANEL.chooser)
end

function onScroll(axis, steps)
  if axis ~= "vertical" then
    return
  end
  local count = math.abs(tonumber(steps) or 0)
  if count == 0 then
    return
  end
  -- One aggregated event per gesture instead of one process per notch.
  local sign = (tonumber(steps) or 0) < 0 and "+" or "-"
  actions.tmus("vol-by", sign .. tostring(SCROLL_STEP * count))
end

function update()
  draw()
end

noctalia.setUpdateInterval(1000)
noctalia.state.watch(KEY, refresh)
refresh(noctalia.state.get(KEY))
