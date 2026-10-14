--!nonstrict
-- Short real FFT-energy history: mist ridges, not seek progress or invented waves.
local M = {}
local BANDS = 24
local POINTS = 192
local STALE_MS = 300
local SAMPLE_MS = 40

function M.new()
  local e = { target = table.create(BANDS, 0), level = table.create(BANDS, 0),
    history = table.create(POINTS, 0), far = table.create(POINTS, 0), near = table.create(POINTS, 0),
    at = nil, steppedAt = nil, sampledAt = nil }

  function e:clear()
    for i = 1, BANDS do self.target[i], self.level[i] = 0, 0 end
    for i = 1, POINTS do self.history[i], self.far[i], self.near[i] = 0, 0, 0 end
    self.at, self.steppedAt, self.sampledAt = nil, nil, nil
  end

  function e:accept(csv, now, playing)
    if not playing or type(csv) ~= "string" or csv == "" then
      self:clear()
      return false
    end
    local count = 0
    for token in csv:gmatch("[^,]+") do
      count += 1
      local value = tonumber(token)
      if count > BANDS or value == nil or value ~= value or math.abs(value) == math.huge then
        self:clear()
        return false
      end
      self.target[count] = math.clamp(value, 0, 1)
    end
    if count ~= BANDS then self:clear(); return false end
    self.at = now
    return true
  end

  function e:step(now, playing)
    if not playing or self.at == nil or now - self.at > STALE_MS then
      self:clear()
      return
    end
    local dt = math.clamp(now - (self.steppedAt or (now - 50)), 0, 100)
    self.steppedAt = now
    for i = 1, BANDS do
      local tau = self.target[i] > self.level[i] and 18 or 65
      self.level[i] += (self.target[i] - self.level[i]) * (1 - math.exp(-dt / tau))
    end
    if self.sampledAt == nil or now - self.sampledAt >= SAMPLE_MS then
      local energy = 0
      for _, value in ipairs(self.level) do energy += value * value end
      local height = math.sqrt(energy / BANDS)
      for i = 1, POINTS - 1 do self.history[i] = self.history[i + 1] end
      self.history[POINTS] = height
      self.sampledAt = now
    end
    -- Contrast comes only from the recent signal; a minimum range avoids
    -- turning tiny noise differences into full-height mountains.
    local floor, peak = 1, 0
    for _, value in ipairs(self.history) do
      floor, peak = math.min(floor, value), math.max(peak, value)
    end
    local range = math.max(0.05, peak - floor)
    local function height(value)
      if value <= 0.008 then return 0 end
      return math.clamp(0.12 + 0.8 * (value - floor) / range, 0, 0.92)
    end
    for i = 1, POINTS do
      local edge = math.min(1, (i - 1) / 3, (POINTS - i) / 3)
      local value = height(self.history[i]) * edge
      self.far[i] = value
      local previous = height(self.history[math.max(1, i - 2)])
      self.near[i] = math.min(value, previous * edge) * 0.72
    end
  end
  return e
end
return M
