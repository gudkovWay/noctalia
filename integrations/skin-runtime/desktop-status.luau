--!nonstrict
-- Persistent desktop status rail (Stagecraft topology-overview r07, left
-- side). 244x720, permanent like the music scene: keyboard_focus = "none",
-- dismiss_on_outside_click = false, no capture_keys — it never takes a key
-- and stays pointer-actionable only.
--
-- Sections follow the r07 hierarchy with honest data only:
--   System   CPU / RAM meters from the host's own system monitor; a disabled
--            monitor renders an explicit Unknown, never zeros.
--   Network  real host throughput rates from systemStats when provided; the
--            link state has no plugin binding, so it reads Unknown — no HTTP
--            probe, no invented totals.
--   Music    the q/tmus player volume (labelled as the music player's
--            volume — it is not the host Sound level).
--   Alerts   no notification binding exists in the plugin API: Unknown.
--   Openers  Overview (this plugin's dashboard), Music library (q/tmus,
--            Win+Alt+M) and AI triage (q/ai-triage, Win+Alt+A) — real
--            destinations only. Session/Power rows are omitted: the plugin
--            API exposes no session or power action.
--   Clock    noctalia.formatTime with the shell's own time/date formats.
--
-- Full filesystem/load/provider details stay in the genuine summary panel.

local material = require("./shared/material.luau")
local status = require("./shared/status.luau")
local actions = require("./shared/actions.luau")

local KEY = "skin.status"

local envelope = status.envelope(nil)
local stats = {}
local panelOpen = false
local signature = nil
local draw

local function refresh(value)
  envelope = status.envelope(value)
  signature = nil
  if panelOpen then
    draw()
  end
end

local function refreshStats()
  local sample = noctalia.systemStats()
  if type(sample) ~= "table" then
    stats = {}
    return
  end
  stats = {
    sampledAtMs = sample.sampledAtMs,
    cpu = type(sample.cpu) == "table" and sample.cpu or nil,
    ram = type(sample.ram) == "table" and sample.ram or nil,
    net = type(sample.net) == "table" and sample.net or nil,
  }
end

local function shellPattern(raw, fallback)
  local pattern = type(raw) == "string" and raw or fallback
  -- Shell settings wrap the strftime body ("{:%H:%M}"); formatTime wants the
  -- bare pattern. The cleaned string is bound to a local and returned alone:
  -- `pattern:gsub(...)` yields (string, count), and a bare `return` of it would
  -- forward the count as formatTime's second argument, which the host reads as
  -- a Unix timestamp — 0 or 1 renders 1970-01-01 instead of now.
  local cleaned = pattern:gsub("^{:", ""):gsub("}$", "")
  return cleaned
end

local function clockLines(tokens)
  local time = noctalia.formatTime(shellPattern(noctalia.timeFormat(), "%H:%M"))
  local date = noctalia.formatTime(shellPattern(noctalia.dateFormat(), "%A, %x"))
  return ui.column({ key = "clock", gap = 2 }, {
    ui.label({
      key = "time",
      text = time ~= "" and time or "Unknown",
      fontSize = 34,
      fontWeight = "bold",
      color = tokens.colors.text,
    }),
    ui.label({
      key = "date",
      text = date ~= "" and date or "Unknown",
      fontSize = tokens.metaSize,
      color = tokens.colors.muted,
      maxLines = 1,
    }),
  })
end

local function sectionLabel(tokens, key, text)
  return ui.label({
    key = key,
    text = text,
    fontSize = tokens.metaSize,
    color = tokens.colors.muted,
    maxLines = 1,
  })
end

local function kvRow(tokens, key, label, value)
  return ui.row({ key = key, gap = tokens.gap, align = "center" }, {
    ui.label({
      key = "label",
      text = label,
      fontSize = tokens.bodySize,
      color = tokens.colors.muted,
      width = 72,
      maxLines = 1,
    }),
    ui.label({
      key = "value",
      text = value or "Unknown",
      fontSize = tokens.bodySize,
      color = value and tokens.colors.text or tokens.colors.muted,
      flexGrow = 1,
      textAlign = "end",
      maxLines = 1,
    }),
  })
end

local function opener(tokens, key, text, glyphName, tooltip, onClick)
  return ui.column({ key = key, gap = 2 }, {
    material.chip(tokens, {
      key = key .. "-open",
      text = text,
      glyph = glyphName,
      glyphSize = 16,
      fontSize = 13,
      -- 244 panel - 2 x 16 framed padding = 212 content; 208 keeps the row
      -- inside the padded perimeter.
      width = 208,
      fill = tokens.colors.surfaceVariant,
      color = tokens.colors.text,
      border = tokens.colors.outline,
      tooltip = tooltip,
      onClick = onClick,
    }),
  })
end

local function systemSection(tokens)
  local children = { sectionLabel(tokens, "system-label", "System") }
  if type(stats.sampledAtMs) ~= "number" then
    table.insert(children, material.unknown(tokens, "monitor-disabled", "System monitor is disabled"))
    return ui.column({ key = "system", gap = 6 }, children)
  end
  local cpu = stats.cpu
  local ram = stats.ram
  table.insert(children, material.meter(tokens, {
    key = "cpu",
    label = "CPU",
    fraction = cpu and cpu.usagePercent and status.fraction(cpu.usagePercent / 100) or nil,
    text = cpu and status.percent(cpu.usagePercent) or nil,
    labelWidth = 44,
    valueWidth = 56,
  }))
  table.insert(children, material.meter(tokens, {
    key = "ram",
    label = "RAM",
    fraction = ram and ram.usagePercent and status.fraction(ram.usagePercent / 100) or nil,
    text = ram and ram.usedMb
        and status.bytes(ram.usedMb * 1024 * 1024)
        or (ram and status.percent(ram.usagePercent) or nil),
    labelWidth = 44,
    valueWidth = 80,
  }))
  return ui.column({ key = "system", gap = 6 }, children)
end

local function networkSection(tokens)
  local net = stats.net
  local rates = nil
  if net and net.rxBytesPerSec and net.txBytesPerSec then
    rates = status.bytes(net.rxBytesPerSec) .. "/s ↓ · " .. status.bytes(net.txBytesPerSec) .. "/s ↑"
  end
  return ui.column({ key = "network", gap = 6 }, {
    sectionLabel(tokens, "network-label", "Network"),
    kvRow(tokens, "net-rates", "Rates", rates),
    -- No link/connection binding exists in the plugin API; honest Unknown.
    kvRow(tokens, "net-link", "Link", nil),
  })
end

local function musicSection(tokens)
  local music = envelope.music
  -- q/tmus player volume — not the host Sound level, and labelled as such.
  return ui.column({ key = "music", gap = 6 }, {
    sectionLabel(tokens, "music-label", "Music"),
    kvRow(tokens, "music-volume", "Music volume", music.volume ~= nil and status.percent(music.volume) or nil),
    kvRow(tokens, "music-state", "State", status.musicState(music)),
  })
end

local function alertsSection(tokens)
  return ui.column({ key = "alerts", gap = 6 }, {
    sectionLabel(tokens, "alerts-label", "Alerts"),
    -- No notification-count binding exists; Unknown, never a guess.
    kvRow(tokens, "alerts-count", "New", nil),
  })
end

function draw()
  if not panelOpen then
    return
  end
  local tokens = material.tokens()
  local clockText = noctalia.formatTime(shellPattern(noctalia.timeFormat(), "%H:%M"))
  local net = stats.net
  local signatureNow = table.concat({
    tokens.family, tokens.density, tostring(tokens.active),
    tostring(envelope.ok), tostring(stats.sampledAtMs),
    tostring(stats.cpu and stats.cpu.usagePercent),
    tostring(stats.ram and stats.ram.usagePercent),
    tostring(net and net.rxBytesPerSec), tostring(net and net.txBytesPerSec),
    tostring(envelope.music.volume), clockText,
  }, "|")
  if signatureNow == signature then
    return
  end
  signature = signatureNow

  local active = envelope.active
  local children = {
    ui.row({ key = "head", gap = 6, align = "center" }, {
      ui.label({
        key = "title",
        text = "STATUS",
        fontSize = tokens.metaSize + 1,
        color = tokens.colors.muted,
        flexGrow = 1,
      }),
      ui.box({
        key = "live-dot",
        width = 7,
        height = 7,
        radius = 4,
        fill = envelope.ok and tokens.colors.accent or tokens.colors.outline,
      }),
    }),
    systemSection(tokens),
    material.divider(tokens, "divider-1"),
    networkSection(tokens),
    musicSection(tokens),
    alertsSection(tokens),
    material.divider(tokens, "divider-2"),
    sectionLabel(tokens, "global-label", "Compositor-global"),
    opener(tokens, "open-overview", "Overview", "gauge", "Stagecraft dashboard (System · Music · AI)", function()
      actions.openPanel(actions.PANEL.dashboard)
    end),
    opener(tokens, "open-music", "Music library", "music", "q/tmus:library — global Win+Alt+M", function()
      actions.openPanel(actions.PANEL.tmus)
    end),
    opener(tokens, "open-ai", "AI triage", "robot", "q/ai-triage:triage — global Win+Alt+A", function()
      actions.openPanel(actions.PANEL.ai)
    end),
    ui.spacer({ key = "spacer", flexGrow = 1 }),
    clockLines(tokens),
  }
  if active and active.name then
    -- The real applied recipe name; never a "Demo" chip.
    table.insert(children, 2, ui.label({
      key = "recipe",
      text = active.name,
      fontSize = tokens.metaSize,
      color = tokens.colors.muted,
      maxLines = 1,
    }))
  end

  panel.render(material.card(tokens, {
    key = "status-panel",
    focused = false,
    flexGrow = 1,
    -- Framed padding floor: the structural band reaches up to 14 logical px
    -- into the perimeter, so content stays clear of it.
    padding = 16,
    shape = "chamfer",
    radius = tokens.chamfer,
  }, children))
end

function onOpen(_context)
  panelOpen = true
  refreshStats()
  signature = nil
  -- The clock reads whole seconds; one tick a second keeps it truthful.
  panel.setWantsSecondTicks(true)
  draw()
end

function onClose()
  panelOpen = false
end

function update()
  if not panelOpen then
    return
  end
  -- systemStats() returns the latest host sample (2s cadence by default);
  -- polling it here keeps meters honest without a second data source.
  refreshStats()
  draw()
end

noctalia.state.watch(KEY, refresh)
refreshStats()
envelope = status.envelope(noctalia.state.get(KEY))
