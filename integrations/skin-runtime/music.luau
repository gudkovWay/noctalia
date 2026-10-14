--!nonstrict
-- Passive permanent music scene (Stagecraft music-scene-top-centre-r06).
--
-- Declared persistent, keyboard_focus = "none", dismiss_on_outside_click =
-- false: it lives outside the single-panel slot, coexists with the library,
-- AI and chooser panels, and never takes the keyboard — so a passive scene
-- can never capture a key or steal focus from whatever the user is typing in.
--
-- Composition follows the approved concept exactly: one chamfered charcoal
-- band (14px cuts) carrying the native structural material while the receipt
-- is active, large real artwork at the left, 36px title / 22px artist with an
-- honest state line, central circular prev / play / next, the real
-- music-library opener at the right, and a lower progress band under a
-- two-band divider. Both flanks are bounded (300px) and the row spaces them
-- apart, so the transport stays on the band's centre. Queue and volume rows are
-- deliberately not here: the concept does not show them and the tmus panel owns
-- those views.
--
-- Content is real or Unknown: track/progress/cover come from the native
-- bridge, transport goes to the original q/tmus:watch service, and the one
-- opener hands off to the real tmus panel (resolved global Win+Alt+M).
--
-- Topology: the native SkinService republishes `music_position` for the
-- selected topology; top-centre and desktop-widget-side share this scene, the
-- host only moves the panel. compact-rail is not implemented and says so.

local material = require("./shared/material.luau")
local status = require("./shared/status.luau")
local actions = require("./shared/actions.luau")

local KEY = "skin.status"
-- Frozen scene geometry: 1080x196 panel. The card padding is the framed floor
-- (16): the structural band reaches up to 14 logical px into the perimeter, so
-- content must stay clear of it. Art fills the remaining band height.
local SCENE_H = 196
local PAD = 16
local ART = SCENE_H - PAD * 2

local envelope = status.envelope(nil)
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

-- Round control: equal width/height and radius = size/2, explicit recipe
-- colors so no host palette bleeds into the Stagecraft silhouette.
local function circle(tokens, opts)
  return material.chip(tokens, {
    key = opts.key,
    glyph = opts.glyph,
    glyphSize = opts.glyphSize,
    width = opts.size,
    height = opts.size,
    radius = opts.size // 2,
    emphasis = opts.emphasis == true,
    fill = opts.emphasis and tokens.colors.accent or tokens.colors.surfaceVariant,
    color = opts.emphasis and tokens.colors.surface or tokens.colors.text,
    border = tokens.colors.outline,
    tooltip = opts.tooltip,
    onClick = opts.onClick,
  })
end

local function transportRow(tokens, music)
  return ui.row({ key = "transport", gap = 18, align = "center" }, {
    circle(tokens, {
      key = "prev",
      glyph = "player-skip-back",
      size = tokens.transportSize,
      glyphSize = 20,
      tooltip = "Previous track (q/tmus)",
      onClick = function()
        actions.tmus("prev")
      end,
    }),
    circle(tokens, {
      key = "toggle",
      glyph = music.playing and "player-pause" or "player-play",
      size = tokens.playSize,
      glyphSize = 26,
      emphasis = true,
      tooltip = music.playing and "Pause (q/tmus)" or "Play (q/tmus)",
      onClick = function()
        actions.tmus("toggle")
      end,
    }),
    circle(tokens, {
      key = "next",
      glyph = "player-skip-forward",
      size = tokens.transportSize,
      glyphSize = 20,
      tooltip = "Next track (q/tmus)",
      onClick = function()
        actions.tmus("next")
      end,
    }),
  })
end

-- The real music-library opener: the tmus panel owns library, queue and EQ,
-- so this is a handoff to that exact destination, never a fake deep-link.
local function libraryOpener(tokens)
  -- Same 300px flank as the track block, pinned to the end: the two equal
  -- flanks keep the central transport on the band's real centre.
  return ui.row({ key = "library", gap = tokens.gap, align = "center", width = 300, justify = "end" }, {
    material.chip(tokens, {
      key = "library-open",
      text = "Library",
      glyph = "music",
      glyphSize = 16,
      fontSize = 14,
      fill = tokens.colors.surfaceVariant,
      color = tokens.colors.text,
      border = tokens.colors.outline,
      tooltip = "Open Music · q/tmus:library · Win+Alt+M",
      onClick = function()
        actions.openPanel(actions.PANEL.tmus)
      end,
    }),
    ui.label({
      key = "library-global",
      text = "Win+Alt+M",
      fontSize = 11,
      color = tokens.colors.muted,
    }),
  })
end

local function cover(tokens, music)
  if music.artPath then
    return ui.image({
      key = "cover",
      path = music.artPath,
      width = ART,
      height = ART,
      radius = 10,
      fit = "cover",
      opacity = music.paused and 0.6 or 1.0,
    })
  end
  return ui.column({
    key = "cover-fallback",
    width = ART,
    height = ART,
    radius = 10,
    align = "center",
    justify = "center",
    shape = "chamfer",
    fill = tokens.colors.surfaceVariant,
    border = tokens.colors.outline,
    borderWidth = 1,
  }, {
    ui.glyph({ key = "cover-glyph", name = "music", size = 34, color = tokens.colors.muted }),
  })
end

-- 36px title, 22px artist, state line with the playing dot: real source
-- states, never focus hints (this scene takes no keyboard). Bounded width so
-- the track block cannot grow and shove the central transport off-centre.
local function trackBlock(tokens, music)
  return ui.column({ key = "track-block", gap = 6, width = 300 }, {
    ui.label({
      key = "track",
      text = status.trackLine(music),
      fontSize = tokens.heroTitleSize,
      fontWeight = "bold",
      color = tokens.colors.text,
      maxLines = 1,
    }),
    ui.label({
      key = "artist",
      text = music.artist or (music.present and music.linked and "Unknown" or ""),
      fontSize = tokens.heroArtistSize,
      color = tokens.colors.muted,
      maxLines = 1,
    }),
    ui.row({ key = "state-row", gap = 6, align = "center" }, {
      ui.box({
        key = "state-dot",
        width = 7,
        height = 7,
        radius = 4,
        fill = music.playing and tokens.colors.accent or tokens.colors.outline,
      }),
      ui.label({
        key = "state",
        text = status.musicState(music),
        fontSize = 12,
        color = music.playing and tokens.colors.accent or tokens.colors.muted,
      }),
    }),
  })
end

-- Lower progress band: elapsed / band / duration. An unknown duration renders
-- placeholders and an empty band, never a fabricated 00:00.
local function progressBand(tokens, music)
  local position = livePosition(music)
  local fraction = nil
  if position ~= nil and music.duration and music.duration > 0 then
    fraction = status.fraction(position / music.duration)
  end
  return ui.row({ key = "progress-band", gap = 12, align = "center" }, {
    ui.label({
      key = "elapsed",
      text = position ~= nil and status.clock(position) or "--:--",
      fontSize = 13,
      color = tokens.colors.text,
    }),
    ui.progress({
      key = "progress",
      flexGrow = 1,
      height = 6,
      radius = 3,
      progress = fraction or 0,
      fill = tokens.colors.accent,
      track = tokens.colors.surfaceVariant,
    }),
    ui.label({
      key = "duration",
      text = music.duration and status.clock(music.duration) or "--:--",
      fontSize = 13,
      color = tokens.colors.text,
    }),
  })
end

function draw()
  if not panelOpen then
    return
  end
  local tokens = material.tokens()
  local music = envelope.music
  local signatureNow = table.concat({
    tokens.family, tokens.density, tokens.topology, tostring(tokens.active),
    tostring(envelope.ok), music.status or "", music.title or "", music.artist or "",
    music.artPath or "", tostring(music.position), tostring(music.duration),
    tostring(music.playing),
  }, "|")
  if signatureNow == signature then
    return
  end
  signature = signatureNow

  local children
  if tokens.rail then
    -- compact-rail belongs to the direction families whose code wave has not
    -- shipped; Stagecraft does not support it. Say so instead of drawing an
    -- unclaimed anatomy as if it were implemented.
    children = {
      material.header(tokens, { key = "rail-head", title = "Music", glyph = "music" }),
      material.unknown(tokens, "rail-unavailable",
        "Topology “compact-rail” is not implemented in this build"),
    }
  else
    -- The bounded r06 band: art left, track block, central circular
    -- transport, library opener right; progress band below the divider.
    children = {
      ui.row({ key = "bands", gap = 22, align = "center", flexGrow = 1 }, {
        cover(tokens, music),
        ui.column({ key = "content-bands", gap = 12, height = ART, flexGrow = 1 }, {
          ui.row({ key = "top", gap = 22, align = "center", flexGrow = 1, justify = "space_between" }, {
            trackBlock(tokens, music),
            transportRow(tokens, music),
            libraryOpener(tokens),
          }),
          material.divider(tokens, "band-divider"),
          progressBand(tokens, music),
        }),
      }),
    }
  end

  panel.render(material.card(tokens, {
    key = "music-scene",
    focused = false,
    flexGrow = 1,
    height = SCENE_H,
    padding = PAD,
    shape = "chamfer",
    radius = tokens.chamfer,
  }, children))
end

function onOpen(_context)
  panelOpen = true
  signature = nil
  -- Second ticks only drive the extrapolated progress; the bridge cadence
  -- stays with the service.
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
  -- Only a moving track needs a second tick; an idle scene re-renders on real
  -- state changes from the service.
  if envelope.music.playing then
    signature = nil
  end
  draw()
end

noctalia.state.watch(KEY, refresh)
envelope = status.envelope(noctalia.state.get(KEY))
