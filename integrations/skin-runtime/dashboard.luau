--!nonstrict
-- Stagecraft dashboard: the one focused app scene of this plugin.
--
-- Anatomy (approved concept dashboard-r05, 1280x850 logical): a dominant
-- three-column music hero — left truthful title/artist + elapsed timeline,
-- center 260x260 real album art, right two real queue rows — over a six-control
-- transport band (shuffle/prev/play/next/repeat/like, 64px round accent play,
-- 44-48px round others, captions below) with the real volume at the right.
-- Below: compact System (3 horizontal metrics) and bounded AI (2 providers)
-- cards, then the keyboard footer. The root is the family's own chamfered
-- charcoal frame; the panel renders its own material (decoration=false), so no
-- host backdrop or palette bleeds in.
--
-- Keyboard is the primary input (owner profile). capture_keys is the exact
-- chord set and every chord has a branch in onKey, so a key handled here is
-- never left to a lower scope:
--   ctrl+j / ctrl+k (+ Ctrl+о / Ctrl+л)  section cycle, wrap
--   j / k (+ о / л)                      row movement in the focused section
--   h / l (+ р / д)                      control movement inside the row
--   Enter                                activate the selected control
--   Escape                               closes the panel (host-owned, never captured)
--
-- Playing / selected / focused are independent states. Nothing is faked:
-- transport, shuffle/repeat/like go to the original q/tmus:watch service with
-- its observed payload protocol; the single music destination hands off to the
-- real q/tmus panel (its owner, including the 10-band EQ); AI opens
-- q/ai-triage:triage; System opens this plugin's summary with real host
-- samples. Missing sources render Unknown — never zeros, demo names or fake
-- deep links.

local material = require("./shared/material.luau")
local status = require("./shared/status.luau")
local actions = require("./shared/actions.luau")

local KEY = "skin.status"
local TMUS_LIBRARY = actions.PANEL.tmus
local AI_TRIAGE = actions.PANEL.ai
local CHOOSER = actions.PANEL.chooser

local SECTIONS = { "music", "system", "ai" }
local SECTION_GLYPH = { music = "music", system = "gauge", ai = "robot" }
-- Resolved global openers from the frozen action map. System has no resolved
-- binding and therefore must not carry a shortcut label.
local SECTION_META = { music = "Win+Alt+M", system = nil, ai = "Win+Alt+A" }

local FOOTER = "dashboard — j/k row · h/l control · Enter activate · Ctrl+j/k section · Esc close"

local envelope = status.envelope(nil)
local stats = {}
local layout = { music = {}, system = {}, ai = {} }
local focus = 1
local cursor = {
  -- Start on the transport row, central play (the r05 dominant control).
  music = { row = 2, col = 3 },
  system = { row = 1, col = 1 },
  ai = { row = 1, col = 1 },
}
local panelOpen = false
local signature = nil
-- Forward declaration: navigation handlers and the volume commit render, and
-- they are defined before the renderer body.
local draw
-- Live slider value while dragging; the commit (and the only tmus call) lands
-- on drag end, so one drag is one process, not one per pixel.
local volumeDraft = nil
-- Keyboard steps have no drag end, so onChange also commits — throttled.
local volumeSentAt = 0
local VOLUME_THROTTLE_MS = 250

-- ── data ─────────────────────────────────────────────────────────────────────

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
  }
end

-- Position between bridge samples is extrapolated from the bridge's own
-- timestamp; it is never invented and never runs past the track duration.
local function livePosition(music)
  if music.position == nil then
    return nil
  end
  local position = music.position
  if music.playing and music.at then
    local delta = os.time() - music.at
    if delta > 0 then
      position = position + delta
    end
  end
  if music.duration and position > music.duration then
    position = music.duration
  end
  return position
end

-- ── rows ─────────────────────────────────────────────────────────────────────
-- Every row: { id, count, render(tokens, focused, selected, selectedCol), activate(col) }.
-- `count` is the number of selectable controls inside the row.

local function openLibrary()
  actions.openPanel(TMUS_LIBRARY)
end

-- One truthful music destination. The existing tmus panel ignores onOpen
-- context, so Home/Search/Library/Clients/EQ would all be the same opener
-- wearing five labels — this dashboard renders the one honest handoff instead.
local function destinationRow()
  return {
    id = "destination",
    count = 1,
    render = function(tokens, _focused, selected)
      return material.chip(tokens, {
        key = "open-music",
        text = "Open Music",
        glyph = "music",
        tooltip = "Open q/tmus:library · Win+Alt+M",
        selected = selected,
        onClick = openLibrary,
      })
    end,
    activate = openLibrary,
  }
end

-- The six visible transport actions. Each control is one keyboard column, so
-- the keyboard-selected control is exactly the visible one.
local function transportControls(music)
  local liked = music.rating == "like"
  local repeatMode = (music.loop == "all" or music.loop == "track") and music.loop or nil
  return {
    {
      id = "shuffle",
      glyph = "arrows-shuffle",
      caption = "Shuffle",
      active = music.shuffle == true,
      enabled = music.present,
      tooltip = music.shuffle and "Shuffle on — turn off" or "Shuffle off — turn on",
      action = function() actions.shuffle(not music.shuffle) end,
    },
    {
      id = "prev",
      glyph = "player-skip-back",
      caption = "Previous",
      enabled = music.present,
      action = function() actions.tmus("prev") end,
    },
    {
      id = "toggle",
      glyph = music.playing and "player-pause" or "player-play",
      caption = music.playing and "Pause" or "Play",
      emphasis = true,
      enabled = music.present,
      action = function() actions.tmus("toggle") end,
    },
    {
      id = "next",
      glyph = "player-skip-forward",
      caption = "Next",
      enabled = music.present,
      action = function() actions.tmus("next") end,
    },
    {
      id = "repeat",
      glyph = "repeat",
      caption = repeatMode == "all" and "Repeat all" or repeatMode == "track" and "Repeat one" or "Repeat",
      active = repeatMode ~= nil,
      enabled = music.present,
      tooltip = "Cycle repeat (off → all → track)",
      action = function() actions.repeatMode(actions.cycleRepeat(repeatMode)) end,
    },
    {
      id = "like",
      glyph = "heart",
      caption = liked and "Liked" or "Like",
      active = liked,
      enabled = music.present and music.trackId ~= nil,
      tooltip = music.trackId and (liked and "Remove like" or "Like this track")
        or "Track id unavailable — rating Unknown",
      action = function() actions.toggleLike(music) end,
    },
  }
end

-- A transport control: round recipe-local button with its caption below. The
-- active state (shuffle on / repeat set / liked) tints the glyph violet; the
-- keyboard cursor draws the accent border independently.
local function transportControl(tokens, control, size, selected)
  local children = {
    material.chip(tokens, {
      key = "ctl-" .. control.id,
      glyph = control.glyph,
      glyphSize = size >= 64 and size * 0.42 or size * 0.38,
      width = size,
      height = size,
      emphasis = control.emphasis == true,
      enabled = control.enabled ~= false,
      selected = selected,
      tooltip = control.tooltip,
      onClick = control.action,
    }),
    ui.label({
      key = "cap-" .. control.id,
      text = control.caption,
      fontSize = tokens.captionSize,
      color = control.emphasis and tokens.colors.text or tokens.colors.muted,
      textAlign = "center",
      maxLines = 1,
    }),
  }
  if control.active then
    -- Domain activity is not focus and not selection.
    table.insert(children, ui.box({
      key = "dot-" .. control.id,
      width = 5,
      height = 5,
      radius = 3,
      fill = tokens.colors.accent,
    }))
  end
  return ui.column({ key = "transport-" .. control.id, gap = 6, align = "center" }, children)
end

local function transportRow(music)
  local controls = transportControls(music)
  return {
    id = "transport",
    count = #controls,
    render = function(tokens, _focused, selected, selectedCol)
      local children = {}
      for index, control in ipairs(controls) do
        local isSelected = selected and index == selectedCol
        local size = control.emphasis and tokens.playSize
          or (index == 2 or index == 4) and tokens.transportSize
          or tokens.transportSmallSize
        table.insert(children, transportControl(tokens, control, size, isSelected))
      end
      return ui.row({ key = "transport-band", gap = tokens.gap + 6, align = "start" }, children)
    end,
    activate = function(col)
      local control = controls[col]
      if control and control.enabled ~= false then
        control.action()
      end
    end,
  }
end

-- One real queue row: source art when the producer published it, truthful
-- title/artist, duration only when present. Its one honest action is the real
-- music library owner (queue manipulation lives there).
local function queueRow(index, item)
  local open = openLibrary
  return {
    id = "queue-" .. index,
    count = 1,
    render = function(tokens, _focused, selected)
      local art
      if item.artPath then
        art = ui.image({
          key = "queue-art",
          path = item.artPath,
          width = 44,
          height = 44,
          radius = 6,
          fit = "cover",
        })
      else
        art = ui.column({
          key = "queue-art-fallback",
          width = 44,
          height = 44,
          radius = 6,
          align = "center",
          justify = "center",
          fill = tokens.colors.surfaceVariant,
          border = tokens.colors.outline,
          borderWidth = 1,
        }, {
          ui.glyph({ key = "queue-glyph", name = "music", size = 18, color = tokens.colors.muted }),
        })
      end
      return ui.row({
        key = "queue-row-" .. index,
        gap = tokens.gap,
        padding = 4,
        align = "center",
        radius = 8,
        fill = selected and tokens.colors.surfaceVariant or nil,
        border = selected and tokens.colors.accent or nil,
        borderWidth = selected and 1 or nil,
        onClick = open,
        tooltip = "Open q/tmus:library",
      }, {
        art,
        ui.column({ key = "queue-text", gap = 2, flexGrow = 1 }, {
          ui.label({
            key = "queue-title",
            text = item.title,
            fontSize = tokens.bodySize,
            color = tokens.colors.text,
            maxLines = 1,
          }),
          ui.label({
            key = "queue-artist",
            text = item.artist or "Unknown artist",
            fontSize = tokens.metaSize,
            color = tokens.colors.muted,
            maxLines = 1,
          }),
        }),
        ui.label({
          key = "queue-duration",
          text = item.duration and status.clock(item.duration) or "--:--",
          fontSize = tokens.metaSize,
          color = tokens.colors.muted,
          width = 48,
          textAlign = "end",
        }),
      })
    end,
    activate = open,
  }
end

local function musicRows()
  local rows = { destinationRow(), transportRow(envelope.music) }
  for index, item in ipairs(envelope.music.queue) do
    if index > 2 then
      break
    end
    table.insert(rows, queueRow(index, item))
  end
  return rows
end

-- Compact System overview: exactly three horizontal metrics (CPU / RAM /
-- temp). A missing sensor reading is Unknown, never a placeholder number.
-- Detailed disks stay in the summary owner, not in this overview.
local function systemRows()
  local rows = {}
  local open = function()
    actions.openPanel(actions.PANEL.summary)
  end

  local metrics = {
    {
      id = "cpu",
      label = "CPU",
      fraction = stats.cpu and status.fraction(stats.cpu.usagePercent and stats.cpu.usagePercent / 100 or nil),
      text = stats.cpu and status.percent(stats.cpu.usagePercent) or "Unknown",
    },
    {
      id = "ram",
      label = "RAM",
      fraction = stats.ram and status.fraction(stats.ram.usagePercent and stats.ram.usagePercent / 100 or nil),
      text = stats.ram and status.bytes(stats.ram.usedMb and stats.ram.usedMb * 1024 * 1024)
        or stats.ram and status.percent(stats.ram.usagePercent)
        or "Unknown",
    },
    {
      id = "temp",
      label = "Temp",
      fraction = nil, -- temperature is a reading, not a fill fraction
      text = stats.cpu and status.degrees(stats.cpu.tempC) or "Unknown",
    },
  }

  table.insert(rows, {
    id = "metrics",
    count = #metrics,
    render = function(tokens, _focused, selected, selectedCol)
      local cells = {}
      for index, metric in ipairs(metrics) do
        local known = metric.id ~= "temp" and metric.fraction ~= nil
        table.insert(cells, ui.column({
          key = "metric-" .. metric.id,
          gap = 4,
          flexGrow = 1,
          padding = 4,
          radius = 8,
          fill = selected and index == selectedCol and tokens.colors.surfaceVariant or nil,
          border = selected and index == selectedCol and tokens.colors.accent or nil,
          borderWidth = selected and index == selectedCol and 1 or nil,
          onClick = open,
        }, {
          ui.label({
            key = "metric-label",
            text = metric.label,
            fontSize = tokens.metaSize,
            color = tokens.colors.muted,
            maxLines = 1,
          }),
          ui.label({
            key = "metric-value",
            text = metric.text,
            fontSize = tokens.bodySize + 3,
            fontWeight = "bold",
            color = known and tokens.colors.text or tokens.colors.muted,
            maxLines = 1,
          }),
          ui.progress({
            key = "metric-bar",
            height = 6,
            radius = 3,
            progress = known and metric.fraction or 0,
            fill = known and tokens.colors.accent or tokens.colors.outline,
            track = tokens.colors.surfaceVariant,
          }),
        }))
      end
      return ui.row({ key = "metrics-band", gap = tokens.gap + 4, align = "start" }, cells)
    end,
    activate = open,
  })

  table.insert(rows, {
    id = "full-metrics",
    count = 1,
    render = function(tokens, _focused, selected)
      return material.chip(tokens, {
        key = "open-summary",
        text = "Full metrics",
        glyph = "gauge",
        tooltip = "Open System summary (disks, load, network)",
        selected = selected,
        onClick = open,
      })
    end,
    activate = open,
  })

  return rows
end

-- Bounded AI region: at most two provider summaries from the curated bridge
-- data plus freshness; the real owner opens for logs/limits/details.
local function aiRows()
  local ai = envelope.ai
  local rows = {}
  local open = function()
    actions.openPanel(AI_TRIAGE)
  end

  if ai.present then
    for index, provider in ipairs(ai.providers) do
      if index > 2 then
        break
      end
      table.insert(rows, {
        id = "provider-" .. index,
        count = 1,
        render = function(tokens, _focused, selected)
          return ui.row({
            key = "provider-row-" .. index,
            gap = tokens.gap,
            padding = 4,
            align = "center",
            radius = 8,
            fill = selected and tokens.colors.surfaceVariant or nil,
            border = selected and tokens.colors.accent or nil,
            borderWidth = selected and 1 or nil,
            onClick = open,
            tooltip = "Open q/ai-triage",
          }, {
            ui.label({
              key = "provider-title",
              text = provider.title or "AI",
              fontSize = tokens.bodySize,
              color = tokens.colors.text,
              flexGrow = 1,
              maxLines = 1,
            }),
            ui.label({
              key = "provider-line",
              text = status.providerLine(provider),
              fontSize = tokens.bodySize,
              color = tokens.colors.muted,
              maxLines = 1,
            }),
          })
        end,
        activate = open,
      })
    end
  end

  if #rows == 0 then
    table.insert(rows, {
      id = "ai-unavailable",
      count = 1,
      render = function(tokens)
        return material.unknown(tokens, "row-ai-unavailable", "No provider report")
      end,
      activate = open,
    })
  end

  return rows
end

-- ── layout / navigation ──────────────────────────────────────────────────────

local function clampCursor()
  for _, id in ipairs(SECTIONS) do
    local rows = layout[id] or {}
    local cur = cursor[id]
    if #rows == 0 then
      cur.row = 1
      cur.col = 1
    else
      cur.row = math.min(math.max(cur.row, 1), #rows)
      local count = rows[cur.row].count or 1
      cur.col = math.min(math.max(cur.col, 1), math.max(count, 1))
    end
  end
end

local function buildLayout()
  layout.music = musicRows()
  layout.system = systemRows()
  layout.ai = aiRows()
  clampCursor()
end

local function focusedId()
  return SECTIONS[focus]
end

local function cycleSection(delta)
  focus = ((focus - 1 + delta) % #SECTIONS) + 1
  signature = nil
  draw()
end

local function moveRow(delta)
  local rows = layout[focusedId()] or {}
  if #rows == 0 then
    return
  end
  local cur = cursor[focusedId()]
  cur.row = cur.row + delta
  if cur.row < 1 then
    cur.row = #rows
  elseif cur.row > #rows then
    cur.row = 1
  end
  cur.col = 1
  signature = nil
  draw()
end

local function moveControl(delta)
  local rows = layout[focusedId()] or {}
  local row = rows[cursor[focusedId()].row]
  if not row then
    return
  end
  local cur = cursor[focusedId()]
  local count = math.max(row.count or 1, 1)
  cur.col = cur.col + delta
  if cur.col < 1 then
    cur.col = count
  elseif cur.col > count then
    cur.col = 1
  end
  signature = nil
  draw()
end

local function activate()
  local rows = layout[focusedId()] or {}
  local row = rows[cursor[focusedId()].row]
  if not row then
    return
  end
  row.activate(cursor[focusedId()].col)
end

-- Every captured chord is handled here before any lower scope; there is no
-- branch that lets a captured key fall through to a hidden region.
function onKey(chord, pressed)
  if not pressed then
    return
  end
  if chord == "ctrl+j" or chord == "ctrl+Cyrillic_o" then
    cycleSection(1)
  elseif chord == "ctrl+k" or chord == "ctrl+Cyrillic_el" then
    cycleSection(-1)
  elseif chord == "J" or chord == "Cyrillic_o" or chord == "Cyrillic_O" then
    moveRow(1)
  elseif chord == "K" or chord == "Cyrillic_el" or chord == "Cyrillic_EL" then
    moveRow(-1)
  elseif chord == "H" or chord == "Cyrillic_er" or chord == "Cyrillic_ER" then
    moveControl(-1)
  elseif chord == "L" or chord == "Cyrillic_de" or chord == "Cyrillic_DE" then
    moveControl(1)
  elseif chord == "Return" then
    activate()
  end
end

-- ── rendering ────────────────────────────────────────────────────────────────

-- Left hero column: truthful identity plus the elapsed timeline. A missing
-- position renders an Unknown line, never a fake 0:00 / 0:00.
local function trackColumn(tokens, music)
  local children = {
    ui.label({
      key = "now-caption",
      text = "NOW PLAYING · " .. string.upper(status.musicState(music)),
      fontSize = tokens.captionSize,
      color = tokens.colors.muted,
      maxLines = 1,
    }),
    ui.label({
      key = "hero-title",
      text = music.title or (music.present and "Nothing playing" or "Unknown"),
      fontSize = tokens.heroTitleSize,
      fontWeight = "bold",
      color = tokens.colors.text,
      maxLines = 2,
    }),
    ui.label({
      key = "hero-artist",
      text = music.artist or "Unknown artist",
      fontSize = tokens.heroArtistSize,
      color = tokens.colors.muted,
      maxLines = 1,
    }),
  }

  local position = livePosition(music)
  if position ~= nil and music.duration and music.duration > 0 then
    table.insert(children, ui.column({ key = "timeline", gap = 4, flexGrow = 1, justify = "end" }, {
      ui.progress({
        key = "timeline-bar",
        height = 6,
        radius = 3,
        progress = status.fraction(position / music.duration),
        fill = tokens.colors.accent,
        track = tokens.colors.surfaceVariant,
      }),
      ui.row({ key = "timeline-labels", gap = tokens.gap, align = "center" }, {
        ui.label({
          key = "elapsed",
          text = status.clock(position),
          fontSize = tokens.metaSize,
          color = tokens.colors.muted,
        }),
        ui.box({ key = "timeline-spacer", flexGrow = 1 }),
        ui.label({
          key = "duration",
          text = status.clock(music.duration),
          fontSize = tokens.metaSize,
          color = tokens.colors.muted,
        }),
      }),
    }))
  elseif not music.present or position == nil then
    table.insert(children, material.unknown(tokens, "timeline-unknown", "Timeline unavailable"))
  end

  -- Bounded width: an unbounded flexGrow here would eat the whole hero row and
  -- push the fixed 260px artwork to the far right instead of the r05 centre.
  return ui.column({
    key = "track-column",
    gap = tokens.gap + 2,
    height = tokens.artSize,
    width = tokens.heroTextWidth,
    justify = "start",
  }, children)
end

-- Center hero column: the dominant 260x260 real artwork, or an explicit
-- charcoal fallback when the bridge has none.
local function artColumn(tokens, music)
  if music.artPath then
    return ui.image({
      key = "hero-art",
      path = music.artPath,
      width = tokens.artSize,
      height = tokens.artSize,
      radius = 10,
      fit = "cover",
      opacity = music.paused and 0.6 or 1.0,
    })
  end
  return ui.column({
    key = "hero-art-fallback",
    width = tokens.artSize,
    height = tokens.artSize,
    radius = 10,
    align = "center",
    justify = "center",
    fill = tokens.colors.surfaceVariant,
    border = tokens.colors.outline,
    borderWidth = 1,
  }, {
    ui.glyph({ key = "hero-art-glyph", name = "music", size = 56, color = tokens.colors.muted }),
  })
end

-- Right hero column: the two real queue rows from the bridge, or an explicit
-- Unknown — the queue is never invented.
local function queueColumn(tokens, music, focused, selectedRow)
  local children = {
    ui.label({
      key = "queue-caption",
      text = "Queue",
      fontSize = tokens.bodySize,
      fontWeight = "bold",
      color = tokens.colors.text,
    }),
  }
  local queue = music.queue
  if #queue == 0 then
    table.insert(children, material.unknown(tokens, "queue-unknown", "Queue unavailable"))
  else
    for index, item in ipairs(queue) do
      if index > 2 then
        break
      end
      -- Queue rows are music-section keyboard rows 3 and 4 (destination and
      -- transport come first); mirror their selected state here.
      local row = layout.music and layout.music[2 + index]
      local isSelected = focused and row ~= nil and cursor.music.row == 2 + index
      local node = row ~= nil and row.render(tokens, focused, isSelected, isSelected and cursor.music.col or nil) or nil
      if node == nil then
        node = material.empty("queue-missing-" .. index)
      end
      table.insert(children, node)
    end
  end
  -- Bounded to the same width as the identity column so the two flanks balance
  -- and the artwork between them lands on the panel centre.
  return ui.column({ key = "queue-column", gap = tokens.gap, width = tokens.heroTextWidth }, children)
end

local function volumeBlock(tokens, music)
  if not music.present or (music.volume == nil and volumeDraft == nil) then
    return material.unknown(tokens, "volume-unknown", "Volume unknown")
  end
  local shown = volumeDraft or music.volume
  return ui.row({ key = "volume-block", gap = tokens.gap, align = "center" }, {
    ui.glyph({ key = "volume-glyph", name = "volume-high", size = tokens.bodySize + 4, color = tokens.colors.muted }),
    ui.slider({
      key = "volume",
      min = 0,
      max = 100,
      step = 5,
      value = shown or 0,
      width = 220,
      -- Frozen explicit slider palette: recipe violet fill, charcoal track —
      -- the host Primary/yellow never appears here.
      fill = tokens.colors.accent,
      track = tokens.colors.surfaceVariant,
      thumb = tokens.colors.text,
      focusColor = tokens.colors.accent,
      onChange = function(value)
        local clamped = actions.volumePercent(value)
        if clamped == nil then
          return
        end
        volumeDraft = clamped
        local now = noctalia.nowMs()
        if now - volumeSentAt >= VOLUME_THROTTLE_MS then
          volumeSentAt = now
          actions.setVolume(clamped)
        end
        signature = nil
        draw()
      end,
      onDragEnd = function()
        -- The host calls this without an argument; the draft carries the value.
        local final = volumeDraft
        volumeDraft = nil
        if final ~= nil then
          volumeSentAt = noctalia.nowMs()
          actions.setVolume(final)
        end
        signature = nil
        draw()
      end,
    }),
    ui.label({
      key = "volume-value",
      text = shown ~= nil and (status.percent(shown) or "Unknown") or "Unknown",
      fontSize = tokens.metaSize,
      color = tokens.colors.muted,
      width = 44,
      textAlign = "end",
    }),
  })
end

local function sectionCard(tokens, id, children, flexGrow)
  return material.card(tokens, {
    key = "card-" .. id,
    focused = focusedId() == id,
    flexGrow = flexGrow,
  }, children)
end

local function drawMusic(tokens)
  local rows = layout.music or {}
  local cur = cursor.music
  local music = envelope.music
  local focused = focusedId() == "music"

  -- Header + the single honest destination (keyboard music row 1).
  local head = ui.row({ key = "music-head-row", gap = tokens.gap, align = "center" }, {
    material.header(tokens, {
      key = "music-head",
      title = "Music",
      glyph = SECTION_GLYPH.music,
      meta = SECTION_META.music,
      focused = focused,
      active = music.playing,
    }),
    ui.box({ key = "music-head-spacer", flexGrow = 1 }),
    -- A keyed placeholder instead of a nil hole: a nil element truncates the
    -- host's `#`-read children array.
    rows[1] and rows[1].render(tokens, focused, focused and cur.row == 1, focused and cur.row == 1 and cur.col or nil)
      or material.empty("music-head-destination"),
  })

  -- Three differentiated middle columns (r05): identity, dominant art, queue.
  -- Both flanks are bounded to the same width and the two separators hug the
  -- artwork, so the row's residual space splits evenly and the art stays on the
  -- panel centre instead of being shoved right by a growing artist column.
  local middle = ui.row({ key = "music-middle", gap = tokens.pad, align = "start", justify = "space_between" }, {
    trackColumn(tokens, music),
    ui.row({ key = "music-art-group", gap = tokens.pad, align = "start" }, {
      material.divider(tokens, "music-divider", true),
      artColumn(tokens, music),
      material.divider(tokens, "queue-divider", true),
    }),
    queueColumn(tokens, music, focused, cur.row),
  })

  -- Transport band with the real volume at the right (keyboard music row 2).
  local band = ui.row({ key = "music-band", gap = tokens.gap, align = "start" }, {
    rows[2] and rows[2].render(tokens, focused, focused and cur.row == 2, focused and cur.row == 2 and cur.col or nil)
      or material.empty("music-band-transport"),
    ui.box({ key = "band-spacer", flexGrow = 1 }),
    volumeBlock(tokens, music),
  })

  -- Native flexGrow divides the available main axis from a zero basis; it
  -- does not add spare space to intrinsic content height. Keep the hero
  -- content-sized so the transport and captions stay inside the card.
  return sectionCard(tokens, "music", {
    head,
    material.divider(tokens, "music-head-divider"),
    middle,
    band,
  })
end

local function drawSystem(tokens)
  local rows = layout.system or {}
  local cur = cursor.system
  local focused = focusedId() == "system"
  local children = {
    material.header(tokens, {
      key = "system-head",
      title = "System",
      glyph = SECTION_GLYPH.system,
      meta = SECTION_META.system,
      focused = focused,
    }),
  }
  for index, row in ipairs(rows) do
    local selected = focused and index == cur.row
    table.insert(children, row.render(tokens, focused, selected, selected and cur.col or nil))
  end
  local age = stats.sampledAtMs and status.ageMs(stats.sampledAtMs)
  table.insert(children, ui.label({
    key = "system-freshness",
    text = age and ("sample " .. age .. " ago") or "sample unavailable",
    fontSize = tokens.metaSize,
    color = tokens.colors.muted,
  }))
  return sectionCard(tokens, "system", children, 1)
end

local function drawAi(tokens)
  local rows = layout.ai or {}
  local cur = cursor.ai
  local focused = focusedId() == "ai"
  local ai = envelope.ai
  local children = {
    material.header(tokens, {
      key = "ai-head",
      title = "AI",
      glyph = SECTION_GLYPH.ai,
      meta = SECTION_META.ai,
      focused = focused,
      active = ai.running or ai.usageBusy == true,
    }),
  }
  if ai.present then
    local parts = {}
    if ai.unread then
      table.insert(parts, tostring(ai.unread) .. " unread")
    end
    if ai.total then
      table.insert(parts, tostring(ai.total) .. " indexed")
    end
    if ai.at then
      local age = status.age(ai.at)
      if age then
        table.insert(parts, "fresh " .. age)
      end
    end
    if #parts > 0 then
      table.insert(children, ui.label({
        key = "ai-summary",
        text = table.concat(parts, " · "),
        fontSize = tokens.metaSize,
        color = tokens.colors.muted,
      }))
    end
  end
  for index, row in ipairs(rows) do
    local selected = focused and index == cur.row
    table.insert(children, row.render(tokens, focused, selected, selected and cur.col or nil))
  end
  return sectionCard(tokens, "ai", children, 1)
end

local function drawHeader(tokens)
  local active = envelope.active
  local name = "Stagecraft"
  local meta = "no active recipe"
  if active then
    name = active.name or "Stagecraft"
    local bits = {}
    if active.family then
      table.insert(bits, active.family)
    end
    if active.topology then
      table.insert(bits, active.topology)
    end
    if active.density then
      table.insert(bits, active.density)
    end
    meta = #bits > 0 and table.concat(bits, " · ") or "applied"
  elseif not envelope.ok then
    meta = envelope.error or "bridge unavailable"
  end

  local titleChildren = {
    ui.label({ key = "title", text = name, fontSize = tokens.titleSize + 2, fontWeight = "bold", color = tokens.colors.text }),
    ui.label({ key = "meta", text = meta, fontSize = tokens.metaSize, color = tokens.colors.muted, maxLines = 1 }),
  }
  -- A configured family that this build cannot render says so instead of
  -- silently passing Stagecraft off as that family.
  if not material.familySupported(tokens, envelope.supportedFamilies) then
    table.insert(titleChildren, ui.label({
      key = "family-note",
      text = tokens.family .. " · unavailable in this build",
      fontSize = tokens.metaSize,
      color = tokens.colors.muted,
    }))
  end

  return ui.row({ key = "top", gap = tokens.gap, align = "center" }, {
    ui.glyph({ key = "mark", name = "palette", size = tokens.titleSize, color = tokens.colors.accent }),
    ui.column({ key = "title-block", gap = 2, flexGrow = 1 }, titleChildren),
    material.chip(tokens, {
      key = "open-skins",
      text = "Skins",
      glyph = "palette",
      tooltip = "Skin chooser (q/wallpaper-studio:skins)",
      onClick = function()
        actions.openPanel(CHOOSER)
      end,
    }),
    material.chip(tokens, {
      key = "open-summary",
      text = "System",
      glyph = "gauge",
      tooltip = "System summary",
      onClick = function()
        actions.openPanel(actions.PANEL.summary)
      end,
    }),
  })
end

function draw()
  if not panelOpen then
    return
  end
  local tokens = material.tokens()
  local signatureNow = table.concat({
    tokens.family, tokens.density, tokens.topology, tostring(tokens.active),
    tostring(focus), tostring(cursor.music.row), tostring(cursor.music.col),
    tostring(cursor.system.row), tostring(cursor.system.col),
    tostring(cursor.ai.row), tostring(cursor.ai.col),
    tostring(envelope.ok), tostring(envelope.music.playing), envelope.music.title or "",
    envelope.music.artist or "",
    tostring(envelope.music.position), tostring(envelope.music.volume),
    tostring(envelope.music.shuffle), envelope.music.loop or "",
    envelope.music.trackId or "", envelope.music.rating or "",
    tostring(#envelope.music.queue),
    tostring(stats.cpu and stats.cpu.usagePercent),
    tostring(stats.cpu and stats.cpu.tempC),
    tostring(stats.ram and stats.ram.usagePercent),
    tostring(#(layout.ai or {})),
  }, "|")
  if signatureNow == signature then
    return
  end
  signature = signatureNow

  -- The dashboard owns its material: a chamfered charcoal root frame with the
  -- r05 paddings, no host panel backdrop beneath it. The structural relief
  -- perimeter is requested only while the native receipt marks Stagecraft
  -- active; otherwise the same frame renders on plain rounded corners.
  panel.render(ui.column({
    key = tokens.active and "dashboard-root-sc" or "dashboard-root-plain",
    fill = tokens.colors.surface,
    material = tokens.active and "stagecraft" or nil,
    shape = "chamfer",
    radius = tokens.chamfer,
    padding = 16,
    gap = 14,
    flexGrow = 1,
    border = tokens.active and tokens.colors.outline or nil,
  }, {
    drawHeader(tokens),
    drawMusic(tokens),
    -- Split widths horizontally, but keep the lower band's natural height.
    ui.row({ key = "lower", gap = 14, align = "start" }, {
      drawSystem(tokens),
      drawAi(tokens),
    }),
    ui.column({ key = "footer-spacer", flexGrow = 1 }, {}),
    material.footer(tokens, "footer", FOOTER),
  }))
end

-- ── lifecycle ────────────────────────────────────────────────────────────────

function onOpen(_context)
  panelOpen = true
  refreshStats()
  buildLayout()
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
  -- Only the stats and the extrapolated progress move on a second tick; a
  -- full bridge re-read stays with the service.
  refreshStats()
  signature = nil
  buildLayout()
  draw()
end

noctalia.state.watch(KEY, function(value)
  envelope = status.envelope(value)
  signature = nil
  if panelOpen then
    buildLayout()
    draw()
  end
end)

envelope = status.envelope(noctalia.state.get(KEY))
buildLayout()
