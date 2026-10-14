--!nonstrict
-- Outbound bridges of the plugin VM.
--
--   * native skin-status read: argv form only, binary from NOCTALIA_SKIN_BINARY
--     when the desktop or a harness sets it, otherwise the installed `noctalia`.
--   * tmus service IPC: playback actions are sent to the ORIGINAL
--     q/tmus:watch service, which owns the daemon, the queue and the EQ. This
--     plugin never touches a player, never speaks MPRIS, never writes another
--     plugin's state key.
--   * panel handoff: in-process togglePanel, so detail views (library, triage,
--     chooser) stay where they are owned.
--
-- No shell strings: every call is an argv array, so a track id or a setting
-- value can never become a command.

local M = {}

local DEFAULT_BINARY = "noctalia"

--- The IPC binary. The override exists so the native SkinService can be
--- exercised before/without installing it as `noctalia`.
function M.binary()
  local override = noctalia.getenv("NOCTALIA_SKIN_BINARY")
  if type(override) == "string" and override ~= "" then
    return override
  end
  return DEFAULT_BINARY
end

--- `noctalia msg skin-status` -> parsed JSON table via callback. `why` is a
--- human line on failure; the caller publishes it as the Unknown reason.
function M.fetchStatus(callback)
  local bin = M.binary()
  local accepted = noctalia.runAsync({ bin, "msg", "skin-status" }, function(result)
    if not result or result.exitCode ~= 0 then
      local why = result and noctalia.string.trim(result.stderr or "") or ""
      if why == "" then
        why = "skin-status bridge did not answer (" .. bin .. ")"
      end
      callback(nil, why)
      return
    end
    local parsed, err = noctalia.json.decode(result.stdout or "")
    if type(parsed) ~= "table" then
      callback(nil, err or "skin-status returned no JSON")
      return
    end
    callback(parsed, nil)
  end, 4000)
  if not accepted then
    callback(nil, "runAsync refused the skin-status bridge")
  end
end

-- The deployed owner's service entry and its onIpc event names:
--   q/tmus:watch  toggle | next | prev | stop | vol-up | vol-down | vol-by |
--                 vol-set | play | play-context | rate | pl
local TMUS_SERVICE = "q/tmus:watch"

function M.tmus(event, payload)
  if type(event) ~= "string" or event == "" then
    return false
  end
  local argv = { M.binary(), "msg", "plugin", TMUS_SERVICE, "all", event }
  if payload ~= nil then
    table.insert(argv, tostring(payload))
  end
  return noctalia.runAsync(argv)
end

--- Clamped integer volume percent, or nil for a non-numeric value. The slider
--- hands its value over as a string.
function M.volumePercent(value)
  local n = tonumber(value)
  if n == nil then
    return nil
  end
  n = math.floor(n + 0.5)
  if n < 0 then
    n = 0
  end
  if n > 100 then
    n = 100
  end
  return n
end

--- Commit an absolute volume to the tmus owner (its `vol-set` payload is a
--- plain integer string, clamped).
function M.setVolume(percent)
  local n = M.volumePercent(percent)
  if n == nil then
    return false
  end
  return M.tmus("vol-set", tostring(n))
end

--- Shuffle transport flag. The observed q/tmus:watch protocol takes the
--- payload `on` / `off` (not a boolean).
function M.shuffle(enabled)
  return M.tmus("shuffle", enabled and "on" or "off")
end

--- Repeat mode. The observed payload set is exactly `off` / `all` / `track`;
--- anything else is refused instead of forwarded.
function M.repeatMode(mode)
  if mode ~= "off" and mode ~= "all" and mode ~= "track" then
    return false
  end
  return M.tmus("repeat", mode)
end

--- Next mode in the off -> all -> track -> off cycle, for a cycling control.
function M.cycleRepeat(mode)
  if mode == "all" then
    return "track"
  elseif mode == "track" then
    return "off"
  end
  return "all"
end

--- Rate a track. The observed payload is `like:<provider>:<id>` or
--- `none:<provider>:<id>`; the id must be the canonical `provider:id` form the
--- bridge publishes (status normalizes it). Returns false — sends nothing —
--- when there is no real id, so a like is never fabricated for an Unknown track.
function M.rate(trackId, liked)
  if type(trackId) ~= "string" or trackId == "" or not trackId:find(":", 1, true) then
    return false
  end
  return M.tmus("rate", (liked and "like:" or "none:") .. trackId)
end

--- Toggle like for the current track from its normalized bridge state. Like
--- all transport calls here, this is fire-and-forget IPC: the source state is
--- never set optimistically — the next bridge answer carries the new rating.
function M.toggleLike(music)
  if type(music) ~= "table" or music.trackId == nil then
    return false
  end
  return M.rate(music.trackId, music.rating ~= "like")
end

--- In-process panel handoff (no IPC round trip, no shell).
function M.openPanel(id)
  if type(id) ~= "string" or id == "" then
    return
  end
  noctalia.togglePanel(id)
end

M.PANEL = {
  dashboard = "q/skin-runtime:dashboard",
  music = "q/skin-runtime:music",
  summary = "q/skin-runtime:summary",
  -- owners of the detail views preserved by handoff, never re-implemented here
  tmus = "q/tmus:library",
  ai = "q/ai-triage:triage",
  chooser = "q/wallpaper-studio:skins",
}

return M
