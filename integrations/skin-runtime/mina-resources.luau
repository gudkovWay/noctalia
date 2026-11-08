--!nonstrict
local actions = require('./shared/actions.luau')
local width, height = 593, 960
local opened = false
local foreground, muted = '#E6D8CF', '#A99F99'

local function number(value)
  return type(value) == 'number' and value == value and math.abs(value) < math.huge and value >= 0
end

local function percent(value)
  return number(value) and value <= 100 and string.format('%.0f%%', value) or '—'
end

local function temperature(value)
  return number(value) and string.format('%.0f°C', value) or '—'
end

local function draw()
  if not opened then return end
  local raw = noctalia.systemStats()
  local stats = type(raw) == 'table' and number(raw.sampledAtMs) and raw or {}
  local cpu = type(stats.cpu) == 'table' and stats.cpu or {}
  local gpu = type(stats.gpu) == 'table' and stats.gpu or {}
  local ram = type(stats.ram) == 'table' and stats.ram or {}
  local disk = noctalia.diskStats('/')
  disk = type(disk) == 'table' and disk or {}
  local memory = '—'
  local memoryPercent = nil
  if number(ram.usedMb) and number(ram.totalMb) and ram.totalMb > 0 and ram.usedMb <= ram.totalMb then
    memory = string.format('%.1f / %.1f GiB', ram.usedMb / 1024, ram.totalMb / 1024)
    memoryPercent = ram.usedMb / ram.totalMb * 100
  end
  local sx, sy = width / 593, height / 960
  local children = {
    ui.image({key='mina-resource-material',path='skin-assets/mina/tray-cloth.png',width=width,height=height,fit='stretch'}),
  }
  local function label(key, text, x, y, size, color, span, align)
    table.insert(children, ui.label({key=key,text=text,x=x*sx,y=y*sy,fontSize=math.max(12,size*math.min(sx,sy)),
      color=color or foreground,width=(span or 250)*sx,height=(size+9)*sy,maxLines=1,textAlign=align or 'start'}))
  end
  local function meter(key, value, y)
    local valid = number(value) and value <= 100
    table.insert(children, ui.progress({key=key,x=62*sx,y=y*sy,width=244*sx,height=3*sy,
      progress=valid and value/100 or 0,visible=valid,fill=foreground,track='#413530',radius=0}))
  end
  label('mina-resource-heading','РЕСУРСЫ',62,199,20)
  label('mina-resource-cpu-label','CPU',62,250,19)
  label('mina-resource-cpu-load',percent(cpu.usagePercent),209,245,25,nil,97,'end')
  label('mina-resource-cpu-temperature-label','Температура',62,285,14,muted)
  label('mina-resource-cpu-temperature',temperature(cpu.tempC),216,283,16,nil,90,'end')
  meter('mina-resource-cpu-meter',cpu.usagePercent,319)
  label('mina-resource-gpu-label','GPU',62,350,19)
  label('mina-resource-gpu-load',percent(gpu.usagePercent),209,345,25,nil,97,'end')
  label('mina-resource-gpu-temperature-label','Температура',62,385,14,muted)
  label('mina-resource-gpu-temperature',temperature(gpu.tempC),216,383,16,nil,90,'end')
  meter('mina-resource-gpu-meter',gpu.usagePercent,419)
  label('mina-resource-ram-label','RAM',62,450,19)
  label('mina-resource-ram-used',memory,62,486,21)
  meter('mina-resource-ram-meter',memoryPercent,527)
  label('mina-resource-ram-percent',memoryPercent and percent(memoryPercent)..' занято' or '—',62,544,14,muted)
  label('mina-resource-disk-label','ДИСК /',62,580,18)
  label('mina-resource-disk-used',percent(disk.usagePercent),209,575,25,nil,97,'end')
  meter('mina-resource-disk-meter',disk.usagePercent,626)
  label('mina-resource-disk-caption','Использовано на /',62,646,14,muted)
  table.insert(children, ui.button({key='mina-resource-detail',text='Подробная сводка ↗',x=54*sx,y=679*sy,
    width=260*sx,height=40*sy,fontSize=15*math.min(sx,sy),variant='ghost',fill='#00000000',
    hoverFill='#39212A',color=muted,contentAlign='start',onClick='onResourceDetail'}))
  panel.render(ui.box({key='mina-resource-wing',width=width,height=height,fill='#00000000'},children))
end

function onPanelGeometry(w,h)
  if type(w)=='number' and type(h)=='number' and w>0 and h>0 then width,height=w,h end
  draw()
end
function onOpen()
  opened = true
  panel.setWantsSecondTicks(true)
  draw()
end
function onClose()
  opened = false
  panel.setWantsSecondTicks(false)
end
function update() draw() end
function onResourceDetail() actions.openPanel(actions.PANEL.summary) end
