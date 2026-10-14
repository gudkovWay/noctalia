--!strict
local M = {}
local function number(value)
  return type(value) == "number" and value == value and math.abs(value) < math.huge
end
local function reading(prefix, value, unit)
  return prefix .. " " .. (number(value) and string.format("%.0f", value) .. unit or "—")
end
function M.readouts(stats, disk)
  local sampled = type(stats) == "table" and number(stats.sampledAtMs)
  local cpu = sampled and stats.cpu or {}
  local gpu = sampled and stats.gpu or {}
  local ram = sampled and stats.ram or {}
  cpu, gpu, ram = cpu or {}, gpu or {}, ram or {}
  local memory = "RAM —"
  if number(ram.usedMb) and number(ram.totalMb) and ram.totalMb > 0 then
    memory = string.format("RAM %.1f/%.1f GiB", ram.usedMb / 1024, ram.totalMb / 1024)
  end
  return {
    reading("CPU", cpu.usagePercent, "%"), reading("CPU", cpu.tempC, "°C"), memory,
    reading("GPU", gpu.usagePercent, "%"), reading("GPU", gpu.tempC, "°C"),
    reading("DF", type(disk) == "table" and disk.usagePercent or nil, "%"),
  }
end
return M
