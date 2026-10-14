--!nonstrict
-- q/skin-runtime service: the single reader of the native skin-status bridge.
--
-- Why a service and not the widget/panels: the widget lives on every bar (one
-- instance per monitor) and the panels come and go, so a bridge call from each
-- would multiply process spawns for the same fact. This entry polls once,
-- publishes plain data on the plugin state channel as `skin.status`, and every
-- other entry only reads. One writer, one cadence, one place where a bridge
-- failure is turned into an honest Unknown.
--
-- What the bridge is: `noctalia msg skin-status` in argv form, binary from
-- NOCTALIA_SKIN_BINARY or the installed `noctalia`. The native SkinService
-- curates the music/AI summary; tmus, ai-triage and the wallpaper studio stay
-- the owners of their own data and actions.
--
-- What this entry never does: poll a provider, read another plugin's state
-- key, write config, or invent a value when the bridge is silent.

local actions = require("./shared/actions.luau")

local KEY = "skin.status"

-- A playing track needs a fresher position; an idle shell does not, and the
-- difference is one process every few seconds instead of every second.
local PLAYING_MS = 1500
local IDLE_MS = 5000

local state = {
  ok = false,
  error = nil,
  at = 0,
  binary = actions.binary(),
  status = nil,
}

local inFlight = false
local nextPollAt = 0

local function publish()
  noctalia.state.set(KEY, state)
end

local function musicIsLive()
  local status = state.status
  local music = type(status) == "table" and status.music or nil
  if type(music) ~= "table" then
    return false
  end
  return music.linked == true or music.status == "playing" or music.status == "paused"
end

local function intervalMs()
  return musicIsLive() and PLAYING_MS or IDLE_MS
end

local function apply(payload, why)
  state.at = os.time()
  if payload then
    state.ok = true
    state.error = nil
    state.status = payload
  else
    -- The last payload is dropped rather than kept: a stale active recipe or
    -- track rendered as current would be a lie. Consumers show Unknown.
    state.ok = false
    state.error = why or "skin-status unavailable"
    state.status = nil
  end
  publish()
end

local function poll()
  if inFlight then
    return
  end
  inFlight = true
  state.binary = actions.binary()
  nextPollAt = noctalia.nowMs() + intervalMs()
  actions.fetchStatus(function(payload, why)
    inFlight = false
    apply(payload, why)
    nextPollAt = noctalia.nowMs() + intervalMs()
  end)
end

noctalia.setUpdateInterval(500)

function update()
  if noctalia.nowMs() >= nextPollAt then
    poll()
  end
end

function onConfigChanged()
  -- A new recipe arrives as plugin settings; re-read the bridge so the
  -- surfaces see the applied recipe (status.active) instead of the old one.
  nextPollAt = 0
end

function onIpc(event, _payload)
  -- Manual nudge for a caller that just applied or cleared a recipe and does
  -- not want to wait out the idle interval.
  if event == "refresh" or event == "poll" or event == "status" then
    nextPollAt = 0
  end
end

function onExit()
  -- Nothing to release: the bridge keeps no process, socket or file handle.
end

-- First read happens at load so a surface opened immediately has data.
publish()
poll()
