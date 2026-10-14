--!nonstrict
-- System summary: the real System destination of the Stagecraft dashboard.
--
-- Data is the host's own system monitor (noctalia.systemStats / diskStats) and
-- the curated music/AI summary from the skin-status bridge. Nothing is
-- fabricated: a missing probe renders "Unknown", a disabled monitor says so,
-- and no dial shows a value the host did not sample. There is no shortcut
-- label for opening System — no resolved binding exists for it.
--
-- Read-only by design, plus pointer openers into the owning surfaces:
-- dashboard (this plugin), music library (q/tmus), AI triage (q/ai-triage)
-- and the skin chooser (q/wallpaper-studio). The panel takes no capture_keys,
-- so no chord of any kind is consumed here.

local material = require("./shared/material.luau")
local status = require("./shared/status.luau")
local actions = require("./shared/actions.luau")

local KEY = "skin.status"
local envelope = status.envelope(nil)
local stats = {}
local disks = {}
local panelOpen = false
local signature = nil
local draw

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
    swap = type(sample.swap) == "table" and sample.swap or nil,
    net = type(sample.net) == "table" and sample.net or nil,
    loadAvg = type(sample.loadAvg) == "table" and sample.loadAvg or nil,
  }
end

local function refreshDisks()
  local mounts = noctalia.diskMounts()
  if type(mounts) ~= "table" then
    disks = {}
    return
  end
  local out = {}
  for _, mount in ipairs(mounts) do
    if #out >= 4 then
      break
    end
    local path = type(mount) == "table" and mount.path or nil
    if type(path) == "string" and path ~= "" then
      local info = noctalia.diskStats(path)
      table.insert(out, { path = path, info = type(info) == "table" and info or nil })
    end
  end
  disks = out
end

-- A missing sample is a word, not a crash and not a zero.
local function bytesText(value)
  return status.bytes(value) or "Unknown"
end

local function percentText(value)
  return status.percent(value) or "Unknown"
end

local function kvRow(tokens, key, label, value)
  return ui.row({ key = key, gap = tokens.gap, align = "center" }, {
    ui.label({ key = "label", text = label, fontSize = tokens.bodySize, color = tokens.colors.muted, width = 110, maxLines = 1 }),
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

local function buttonRow(tokens)
  return ui.row({ key = "openers", gap = tokens.gap, align = "center" }, {
    material.chip(tokens, {
      key = "open-dashboard",
      text = "Dashboard",
      glyph = "palette",
      tooltip = "Stagecraft dashboard",
      onClick = function()
        actions.openPanel(actions.PANEL.dashboard)
      end,
    }),
    material.chip(tokens, {
      key = "open-music",
      text = "Music library",
      glyph = "music",
      tooltip = "q/tmus:library — global Win+Alt+M",
      onClick = function()
        actions.openPanel(actions.PANEL.tmus)
      end,
    }),
    material.chip(tokens, {
      key = "open-ai",
      text = "AI triage",
      glyph = "robot",
      tooltip = "q/ai-triage:triage — global Win+Alt+A",
      onClick = function()
        actions.openPanel(actions.PANEL.ai)
      end,
    }),
    material.chip(tokens, {
      key = "open-skins",
      text = "Skins",
      glyph = "palette",
      tooltip = "Skin chooser (q/wallpaper-studio:skins)",
      onClick = function()
        actions.openPanel(actions.PANEL.chooser)
      end,
    }),
  })
end

function draw()
  if not panelOpen then
    return
  end
  local tokens = material.tokens()
  local age = stats.sampledAtMs and status.ageMs(stats.sampledAtMs)
  local signatureNow = table.concat({
    tokens.family, tokens.density, tostring(tokens.active),
    tostring(envelope.ok), tostring(stats.sampledAtMs), tostring(#disks),
  }, "|")
  if signatureNow == signature then
    return
  end
  signature = signatureNow

  local cpu = stats.cpu
  local ram = stats.ram
  local swap = stats.swap
  local net = stats.net

  local systemChildren = {
    material.header(tokens, { key = "system-head", title = "System", glyph = "gauge" }),
  }
  if type(stats.sampledAtMs) == "number" then
    table.insert(systemChildren, kvRow(tokens, "cpu", "CPU", cpu and percentText(cpu.usagePercent) or nil))
    if cpu and cpu.tempC then
      table.insert(systemChildren, kvRow(tokens, "cpu-temp", "CPU temp", string.format("%.0f°C", cpu.tempC)))
    end
    if cpu and cpu.freqMhz then
      table.insert(systemChildren, kvRow(tokens, "cpu-freq", "CPU freq", string.format("%.0f MHz", cpu.freqMhz)))
    end
    local ramText = nil
    if ram then
      ramText = bytesText(ram.usedMb and ram.usedMb * 1024 * 1024) .. " / " .. bytesText(ram.totalMb and ram.totalMb * 1024 * 1024)
    end
    table.insert(systemChildren, kvRow(tokens, "ram", "RAM", ramText))
    table.insert(systemChildren, kvRow(tokens, "ram-pct", "RAM used", ram and percentText(ram.usagePercent) or nil))
    if swap and swap.totalMb then
      table.insert(systemChildren, kvRow(tokens, "swap", "Swap",
        bytesText(swap.usedMb and swap.usedMb * 1024 * 1024) .. " / " .. bytesText(swap.totalMb * 1024 * 1024)))
    end
    for index, disk in ipairs(disks) do
      local info = disk.info
      local value = nil
      if info then
        value = percentText(info.usagePercent) .. " · " .. bytesText(info.availableBytes) .. " free"
      end
      table.insert(systemChildren, kvRow(tokens, "disk-" .. index, disk.path, value))
    end
    if net and net.rxBytesPerSec then
      table.insert(systemChildren, kvRow(tokens, "net", "Network",
        bytesText(net.rxBytesPerSec) .. "/s ↓ · " .. bytesText(net.txBytesPerSec) .. "/s ↑"))
    end
    if stats.loadAvg and #stats.loadAvg >= 3 then
      table.insert(systemChildren, kvRow(tokens, "load", "Load", string.format("%.2f %.2f %.2f", stats.loadAvg[1], stats.loadAvg[2], stats.loadAvg[3])))
    end
    table.insert(systemChildren, ui.label({
      key = "freshness",
      text = age and ("sample " .. age .. " ago") or "sample time unknown",
      fontSize = tokens.metaSize,
      color = tokens.colors.muted,
    }))
  else
    -- The host monitor is disabled; say it, do not draw empty dials.
    table.insert(systemChildren, material.unknown(tokens, "monitor-disabled", "System monitor is disabled in the shell config"))
  end

  local music = envelope.music
  local musicChildren = {
    material.header(tokens, { key = "music-head", title = "Music", glyph = "music", active = music.playing }),
    kvRow(tokens, "music-state", "State", status.musicState(music)),
  }
  if music.present and music.linked then
    table.insert(musicChildren, kvRow(tokens, "music-track", "Track", music.title))
    if music.artist then
      table.insert(musicChildren, kvRow(tokens, "music-artist", "Artist", music.artist))
    end
    if music.duration then
      table.insert(musicChildren, kvRow(tokens, "music-time", "Time", status.clock(music.position) .. " / " .. status.clock(music.duration)))
    elseif music.position then
      table.insert(musicChildren, kvRow(tokens, "music-time", "Position", status.clock(music.position)))
    end
  end

  local ai = envelope.ai
  local aiChildren = {
    material.header(tokens, { key = "ai-head", title = "AI", glyph = "robot", active = ai.running or ai.usageBusy == true }),
  }
  if ai.present then
    if ai.total then
      table.insert(aiChildren, kvRow(tokens, "ai-total", "Indexed", tostring(ai.total)))
    end
    if ai.unread then
      table.insert(aiChildren, kvRow(tokens, "ai-unread", "Unread", tostring(ai.unread)))
    end
    if ai.at then
      table.insert(aiChildren, kvRow(tokens, "ai-fresh", "Freshness", status.age(ai.at)))
    end
    for index, provider in ipairs(ai.providers) do
      if index > 5 then
        break
      end
      table.insert(aiChildren, kvRow(tokens, "provider-" .. index, provider.title or "AI", status.providerLine(provider)))
    end
    if #ai.providers == 0 then
      table.insert(aiChildren, material.unknown(tokens, "ai-no-provider", "No provider report"))
    end
  else
    table.insert(aiChildren, material.unknown(tokens, "ai-missing", "AI summary unavailable"))
  end

  local bridgeChildren = {
    material.header(tokens, { key = "bridge-head", title = "Skin runtime", glyph = "palette" }),
    kvRow(tokens, "bridge", "Bridge", envelope.ok and "connected" or (envelope.error or "Unknown")),
  }
  if envelope.active then
    table.insert(bridgeChildren, kvRow(tokens, "bridge-active", "Active", envelope.active.name or "Unknown"))
    table.insert(bridgeChildren, kvRow(tokens, "bridge-family", "Family",
      envelope.active.family and (envelope.active.family .. (envelope.active.topology and (" · " .. envelope.active.topology) or "")) or "Unknown"))
  else
    table.insert(bridgeChildren, kvRow(tokens, "bridge-active", "Active", nil))
  end
  if #envelope.supportedFamilies > 0 then
    table.insert(bridgeChildren, kvRow(tokens, "bridge-families", "Families", table.concat(envelope.supportedFamilies, ", ")))
  end

  panel.render(ui.column({ key = "summary", gap = tokens.gap }, {
    ui.row({ key = "top", gap = tokens.gap, align = "center" }, {
      ui.glyph({ key = "mark", name = "gauge", size = tokens.titleSize, color = tokens.colors.accent }),
      ui.label({ key = "title", text = "System", fontSize = tokens.titleSize + 2, fontWeight = "bold", color = tokens.colors.text, flexGrow = 1 }),
    }),
    buttonRow(tokens),
    ui.scroll({ key = "body", gap = tokens.gap, flexGrow = 1 }, {
      material.card(tokens, { key = "card-system" }, systemChildren),
      material.card(tokens, { key = "card-music" }, musicChildren),
      material.card(tokens, { key = "card-ai" }, aiChildren),
      material.card(tokens, { key = "card-bridge" }, bridgeChildren),
    }),
  }))
end

function onOpen(_context)
  panelOpen = true
  refreshStats()
  refreshDisks()
  signature = nil
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
  refreshStats()
  draw()
end

noctalia.state.watch(KEY, function(value)
  envelope = status.envelope(value)
  signature = nil
  if panelOpen then
    draw()
  end
end)

envelope = status.envelope(noctalia.state.get(KEY))
