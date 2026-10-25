local M = {}

local function read_file(path, limit)
    local file = io.open(path, "rb")
    if not file then return nil end
    local value = file:read(limit)
    file:close()
    return value
end

function M.destination(window)
    if not window or window.class == "pult-session" then return nil end
    local legacy = window.class == "agent-test" or window.class == "aquamarine"
    local pid = tonumber(window.pid)
    for _ = 1, 64 do
        if not pid or pid <= 1 or pid % 1 ~= 0 then break end
        local entries = read_file("/proc/" .. pid .. "/environ", 1048576)
        if not entries then break end
        for entry in entries:gmatch("([^%z]+)") do
            local key = entry:match("^OMP_GUI_KEY=([0-9a-f]+)$")
            if key and #key == 24 then return "special:omp-" .. key end
            if entry:match("^AGENT=") or entry:match("^OMPCODE=") then legacy = true end
        end
        local status = read_file("/proc/" .. pid .. "/status", 16384)
        pid = status and tonumber(status:match("\nPPid:%s*(%d+)"))
    end
    if legacy then return "special:agent-tests" end
    return nil
end

return M
