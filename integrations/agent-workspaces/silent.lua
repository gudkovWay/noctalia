-- silent: window rules that pin a window to a workspace without following it.
--
-- Hyprland 0.56.2's Lua API cannot express this as a plain hl.window_rule:
--   * `silent = true`            -> "hl.window_rule: unknown field 'silent'"
--   * `workspace = "13 silent"`  -> accepted, but the trailing word is dropped,
--                                   so the window lands on 13 and takes focus
--                                   along with it.
-- `movetoworkspacesilent` is only reachable as a dispatcher, so this module
-- registers the rules declaratively and applies them on window.open with
-- follow = false, which is exactly what the dispatcher does.
--
-- Usage mirrors hl.window_rule:
--
--   local silent = require("config.silent")
--   silent.window_rule({ match = { class = "discord" },              workspace = 1 })
--   silent.window_rule({ match = { class = { "zen", "zen-alpha" } }, workspace = 13 })
--
-- match.class takes one class or a list of classes, compared exactly against
-- the window's class (no regex, unlike hl.window_rule).

local M = {}

local rules = {}
local agent_workspace = require("config.agent_workspace")

---@param spec table { match = { class = string|string[] }, workspace = integer|string }
function M.window_rule(spec)
    assert(type(spec) == "table", "silent.window_rule: argument must be a table")
    assert(spec.workspace ~= nil, "silent.window_rule: missing field 'workspace'")

    local class = spec.match and spec.match.class
    assert(class ~= nil, "silent.window_rule: missing field 'match.class'")

    local classes = type(class) == "table" and class or { class }
    for _, c in ipairs(classes) do
        rules[c] = spec.workspace
    end

    return spec
end

-- The layout may focus a new target before window.open is emitted.
hl.on("window.open_early", function(w)
    local workspace = agent_workspace.destination(w)
    if workspace then
        hl.dispatch(hl.dsp.window.set_prop({ prop = "no_focus", value = "true", window = w }))
        hl.dispatch(hl.dsp.window.move({ workspace = workspace, follow = false, window = w }))
        -- mapWindow has another focus path after window.open; restore normal rules next turn.
        hl.timer(function()
            if w.mapped then
                hl.dispatch(hl.dsp.window.set_prop({ prop = "no_focus", value = "unset", window = w }))
            end
        end, { timeout = 1, type = "oneshot" })
    end
end)

hl.on("window.open", function(w)
    if agent_workspace.destination(w) then return end
    local workspace = w and w.class and rules[w.class]
    if workspace then
        hl.dispatch(hl.dsp.window.move({ workspace = workspace, follow = false, window = w }))
    end
end)

_G.omp_agent_workspace_version = 1

return M
