local M = {}

-- Copy to config/agent_workspace_gallery.lua and call .apply() once from the owner's config.
-- GTK owns fullscreen/decorations; no unsupported no_border/no_shadow window-rule fields.
function M.apply()
    local executable = assert(os.getenv("HOME")) .. "/.local/bin/agent-workspace-gallery"
    local quoted = "'" .. executable:gsub("'", "'\\''") .. "'"
    hl.bind("SUPER + ALT + S", hl.dsp.exec_cmd(quoted))
    hl.window_rule({ match = { class = "^omp-agent-gallery$" }, float = true, center = true })
    hl.bind("SUPER + CTRL + S", function()
        local window = hl.get_active_window()
        if not window or not window.workspace or not window.workspace.special or not window.monitor then return end
        local target = hl.get_active_workspace(window.monitor)
        if not target then return end
        hl.dispatch(hl.dsp.window.move({ window = "address:" .. window.address, workspace = target.id, follow = true }))
    end)
end

return M
