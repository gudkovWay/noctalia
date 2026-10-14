--!nonstrict
-- Normalization and formatting for the native skin-status bridge.
--
-- The service publishes the parsed bridge answer as this plugin's
-- `skin.status` state value; every consumer goes through this module. A
-- missing field is a first-class Unknown, a nil/disabled source is a
-- first-class "no player" state, and a schema drift in the native bridge can
-- degrade a surface but never crash it. Nothing here invents a value to fill a
-- hole: absent stats stay absent.
--
-- Wire shape (schema_version 1), produced by the native SkinService:
--   { schema_version, supported_families:[...], active:<recipe|null>,
--     wallpaper_path:<path|null>, music:<obj|null>, ai:<obj|null>,
--     persistent_music_open:bool }
-- The service adds an envelope: { ok, error, at, binary, status }.

local M = {}

local function str(value)
  if type(value) == "string" and value ~= "" then
    return value
  end
  return nil
end

local function num(value)
  if type(value) == "number" then
    return value
  end
  if type(value) == "string" then
    local parsed = tonumber(value)
    if parsed ~= nil then
      return parsed
    end
  end
  return nil
end

local function flag(value)
  if type(value) == "boolean" then
    return value
  end
  return nil
end

--- Canonical current-track id "provider:id" (frozen additive contract). Anything
--- else — empty, no provider part, the full ratings map — is refused here, so a
--- like payload can never be built from a malformed id.
local function canonicalTrackId(value)
  local id = str(value)
  if not id then
    return nil
  end
  local provider, rest = id:match("^(%S+):(.+)$")
  if provider and provider ~= "" and rest and rest ~= "" then
    return id
  end
  return nil
end

--- The single rating state of the current track. Bounded to the two values the
--- rate IPC actually accepts; a queue-wide rating never crosses this boundary.
local function boundedRating(value)
  if value == "like" or value == "none" then
    return value
  end
  return nil
end

--- Music: null means "no player / bridge has nothing". A present object keeps
--- its own states: linked=false is disconnected, empty title is stopped/idle.
function M.music(raw)
  if type(raw) ~= "table" then
    return {
      present = false,
      linked = false,
      status = nil,
      playing = false,
      paused = false,
      queue = {},
    }
  end

  local queue = {}
  if type(raw.queue) == "table" then
    for _, entry in ipairs(raw.queue) do
      if type(entry) == "table" then
        local title = str(entry.title) or str(entry.name)
        if title then
          table.insert(queue, {
            title = title,
            artist = str(entry.artist),
            artPath = str(entry.artPath) or str(entry.art_path),
            -- Copied only when the producer actually published it; a queue row
            -- never shows an invented duration.
            duration = num(entry.duration),
          })
        end
      elseif type(entry) == "string" and entry ~= "" then
        table.insert(queue, { title = entry })
      end
      if #queue >= 2 then
        break
      end
    end
  end

  local status = str(raw.status)
  local position = num(raw.position)
  local duration = num(raw.duration)

  return {
    present = true,
    linked = flag(raw.linked) == true,
    status = status,
    playing = status == "playing",
    paused = status == "paused",
    stopped = status == nil or status == "stopped",
    title = str(raw.title),
    artist = str(raw.artist),
    album = str(raw.album),
    artPath = str(raw.artPath) or str(raw.art_path),
    -- Additive bounded current-track identity/rating (frozen contract). Both
    -- stay nil when the bridge does not publish them.
    trackId = canonicalTrackId(raw.trackId),
    rating = boundedRating(raw.rating),
    position = position,
    duration = duration,
    volume = num(raw.volume),
    shuffle = flag(raw.shuffle) == true,
    loop = str(raw.loop),
    at = num(raw.at),
    queue = queue,
  }
end

--- AI: safe counts/freshness plus the provider quota/balance rows the native
--- service curated. Logs, tails, paths and secrets never cross the bridge and
--- are therefore not read here.
function M.ai(raw)
  if type(raw) ~= "table" then
    return { present = false, providers = {} }
  end

  local counts = type(raw.counts) == "table" and raw.counts or raw
  local usage = type(raw.usage) == "table" and raw.usage or {}
  -- `running` arrives at the summary top level (counts carries only counters);
  -- accept either so the active marker cannot be driven by the wrong field.
  local running = flag(raw.running)
  if running == nil then
    running = flag(counts.running)
  end
  local providerList = nil
  if type(raw.providers) == "table" then
    providerList = raw.providers
  elseif type(usage.providers) == "table" then
    providerList = usage.providers
  end

  local providers = {}
  if providerList then
    for _, provider in ipairs(providerList) do
      if type(provider) == "table" then
        local rows = {}
        if type(provider.rows) == "table" then
          for _, row in ipairs(provider.rows) do
            if type(row) == "table" then
              table.insert(rows, {
                label = str(row.label),
                text = str(row.text),
                fraction = num(row.fraction),
                resetsAt = num(row.resetsAt),
              })
            end
          end
        end
        table.insert(providers, {
          id = str(provider.id),
          title = str(provider.title) or str(provider.id),
          plan = str(provider.plan),
          error = str(provider.error),
          rows = rows,
        })
      end
    end
  end

  return {
    present = true,
    total = num(counts.total),
    unread = num(counts.unread),
    yes = num(counts.yes),
    no = num(counts.no),
    unsure = num(counts.unsure),
    warn = num(counts.warn),
    memoryPending = num(counts.memory_pending),
    memoryDisputed = num(counts.memory_disputed),
    running = running == true,
    -- Freshness arrives as `at` on the summary (and as `freshness`/counts.at in
    -- older shapes); the first present stamp wins.
    at = num(raw.at) or num(raw.freshness) or num(counts.at),
    usageAt = num(usage.at),
    usageError = str(usage.error),
    usageBusy = flag(usage.busy) == true,
    providers = providers,
  }
end

--- Active recipe: kept as the native object, with the fields the surfaces need
--- surfaced and bounded. Null stays null (no active recipe yet).
function M.recipe(raw)
  if type(raw) ~= "table" then
    return nil
  end
  local colors = type(raw.colors) == "table" and raw.colors or {}
  return {
    name = str(raw.name),
    family = str(raw.family),
    topology = str(raw.topology),
    density = str(raw.density),
    variantId = str(raw.variant_id),
    contentId = str(raw.content_id),
    previewPath = str(raw.preview_path),
    colors = {
      surface = str(colors.surface),
      surfaceVariant = str(colors.surface_variant),
      text = str(colors.text),
      muted = str(colors.muted),
      accent = str(colors.accent),
      outline = str(colors.outline),
    },
  }
end

--- Normalize the whole published envelope. Tolerates a bare payload as well,
--- so a direct bridge read (e.g. from a debug REPL) cannot break a surface.
function M.envelope(raw)
  if type(raw) ~= "table" then
    return {
      ok = false,
      error = "bridge has not answered yet",
      supportedFamilies = {},
      music = M.music(nil),
      ai = M.ai(nil),
    }
  end

  local payload = type(raw.status) == "table" and raw.status or raw

  return {
    ok = raw.ok ~= false and payload ~= nil,
    error = str(raw.error),
    at = num(raw.at),
    binary = str(raw.binary),
    schemaVersion = num(payload.schema_version),
    supportedFamilies = type(payload.supported_families) == "table" and payload.supported_families or {},
    active = M.recipe(payload.active),
    wallpaperPath = str(payload.wallpaper_path),
    music = M.music(payload.music),
    ai = M.ai(payload.ai),
    persistentMusicOpen = flag(payload.persistent_music_open),
  }
end

-- ── formatting ───────────────────────────────────────────────────────────────

--- mm:ss / h:mm:ss; nil (unknown) renders a placeholder, never 00:00.
function M.clock(seconds)
  local total = num(seconds)
  if total == nil or total < 0 then
    return "--:--"
  end
  total = math.floor(total)
  local h = total // 3600
  local m = (total % 3600) // 60
  local s = total % 60
  if h > 0 then
    return string.format("%d:%02d:%02d", h, m, s)
  end
  return string.format("%d:%02d", m, s)
end

--- Short age of a unix-seconds stamp ("12s", "4m"), nil when unknown.
function M.age(at)
  local stamp = num(at)
  if stamp == nil then
    return nil
  end
  local delta = os.time() - stamp
  if delta < 0 then
    delta = 0
  end
  if delta < 60 then
    return string.format("%ds", delta)
  end
  if delta < 3600 then
    return string.format("%dm", delta // 60)
  end
  if delta < 86400 then
    return string.format("%dh", delta // 3600)
  end
  return string.format("%dd", delta // 86400)
end

--- Age from a wall-clock millisecond stamp (systemStats.sampledAtMs).
function M.ageMs(sampledAtMs)
  local stamp = num(sampledAtMs)
  if stamp == nil then
    return nil
  end
  local delta = noctalia.nowMs() - stamp
  if delta < 0 then
    delta = 0
  end
  if delta < 1000 then
    return "<1s"
  end
  local seconds = math.floor(delta / 1000)
  if seconds < 60 then
    return string.format("%ds", seconds)
  end
  return string.format("%dm", seconds // 60)
end

function M.bytes(value)
  local n = num(value)
  if n == nil then
    return nil
  end
  local units = { "B", "KiB", "MiB", "GiB", "TiB" }
  local index = 1
  while n >= 1024 and index < #units do
    n = n / 1024
    index = index + 1
  end
  if index == 1 then
    return string.format("%d %s", math.floor(n), units[index])
  end
  return string.format("%.1f %s", n, units[index])
end

--- "47°C" from a Celsius reading; nil (no real sensor reading) stays Unknown.
function M.degrees(celsius)
  local v = num(celsius)
  if v == nil then
    return nil
  end
  return string.format("%d°C", math.floor(v + 0.5))
end

--- "42%" from a 0..100 value.
function M.percent(value)
  local v = num(value)
  if v == nil then
    return nil
  end
  return string.format("%d%%", math.floor(v + 0.5))
end

--- 0..1 fraction into a bar progress value (clamped).
function M.fraction(value)
  local v = num(value)
  if v == nil then
    return nil
  end
  if v < 0 then
    return 0
  end
  if v > 1 then
    return 1
  end
  return v
end

--- Music state as an honest word: no player, disconnected, playing, paused,
--- stopped/idle.
function M.musicState(music)
  if not music.present then
    return "No player"
  end
  if not music.linked then
    return "Disconnected"
  end
  if music.playing then
    return "Playing"
  end
  if music.paused then
    return "Paused"
  end
  if music.title then
    return "Stopped"
  end
  return "Idle"
end

function M.trackLine(music)
  if not music.present then
    return "Unknown"
  end
  if not music.linked then
    return "tmusd offline"
  end
  if not music.title then
    return "Nothing playing"
  end
  if music.artist then
    return music.artist .. " — " .. music.title
  end
  return music.title
end

--- One safe line for a provider: its first quota/balance row, its error, or
--- Unknown. Never a fabricated number.
function M.providerLine(provider)
  local first = provider.rows and provider.rows[1]
  if first then
    if first.text then
      return first.text
    end
    local fraction = M.percent(first.fraction and first.fraction * 100 or nil)
    if fraction then
      return fraction
    end
  end
  if provider.error then
    return provider.error
  end
  return "Unknown"
end

return M
