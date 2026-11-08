--!nonstrict
local status = require('./shared/status.luau')
local spectrum = require('./shared/music-spectrum.luau').new()
local envelope = status.envelope(nil)
local width, height = 3440, 180
local lastDraw, lastTrack = 0, ''
local updateInterval = 100
local FG, MUTED, ACCENT = '#E9DCCB', '#BCAEAE', '#D76C82'

local function available()
  return envelope.ok and envelope.music.present and envelope.music.linked
end
local function playing()
  return available() and envelope.music.playing
end
local function time(value)
  if type(value) ~= 'number' or value ~= value or value < 0 or value == math.huge then return '--:--' end
  value = math.floor(value)
  return string.format('%d:%02d', math.floor(value / 60), value % 60)
end
local function position(music)
  if not available() or music.position == nil then return nil end
  local value = music.position
  if music.playing and music.at then value += math.max(0, os.time() - music.at) end
  if music.duration and music.duration > 0 then value = math.min(value, music.duration) end
  return math.max(0, value)
end
local function draw()
  local now = noctalia.nowMs()
  lastDraw = now
  local s = math.min(1, math.max(1, height - 36) / 144, width / 1700)
  -- The native top layer starts after the 104px left rail.
  local origin = math.max(440 * s, (width - 860 * s - 104) / 2)
  local top = 12
  local music = envelope.music
  local title = not envelope.ok and 'Music unavailable' or not music.present and 'No player'
    or not music.linked and 'Player disconnected' or music.title or 'No track'
  local artist = available() and music.artist or 'Choose music in the library'
  local album = available() and music.album or nil
  local p = position(music)
  local duration = available() and music.duration and music.duration > 0 and music.duration or nil
  local ratio = p and duration and math.clamp(p / duration, 0, 1) or 0
  local peak, mid = 0, 0
  for i = 1, 24 do
    peak = math.max(peak, spectrum.level[i])
    if i >= 9 and i <= 16 then mid += spectrum.level[i] / 8 end
  end
  local folds, lining
  if playing() and spectrum.at and peak > 0.008 then
    folds, lining = table.create(64, 0), table.create(64, 0)
    local phase = now * 0.0024
    local lift = 0.24 + 0.12 * math.sqrt(peak)
    local returnLift = 0.18 + 0.1 * math.sqrt(mid)
    for i = 1, 64 do
      local x = (i - 1) / 63
      folds[i] = 0.5 + lift * math.sin(2 * math.pi * 1.6 * x - phase)
      lining[i] = 0.34 + returnLift * math.sin(2 * math.pi * 1.8 * x + phase * 0.78 + 1.6)
    end
  end
  local interval = folds and 32 or 100
  if interval ~= updateInterval then
    updateInterval = interval
    noctalia.setUpdateInterval(interval)
  end
  local function label(key, x, y, text, size, color, maxWidth)
    return ui.label({key=key,x=origin+x*s,y=top+y*s,text=text,fontSize=size*s,
      fontFamily='Maple Mono',color=color,maxWidth=maxWidth*s,maxLines=1})
  end
  local nodes = {
    ui.image({key='workspace-frame',path='skin-assets/mina/signal-controls.svg',x=12,y=0,width=420,height=64,fit='stretch'}),
    ui.glyph({key='workspace-sigil',name='crosshair',x=42,y=21,size=22,color='#CFAB83'}),
    ui.image({key='status-frame',path='skin-assets/mina/signal-controls.svg',x=width-436,y=0,width=424,height=64,fit='stretch'}),
    ui.box({key='status-divider',x=width-122,y=22,width=1,height=20,fill='#99745C'}),
    ui.image({key='mina-cloth',path='skin-assets/mina/signal-music.svg',x=origin,y=top,width=860*s,height=144*s,fit='stretch'}),
    label('mina-title',124,album and 43 or 51,title,17,FG,696),
    label('mina-artist',124,album and 69 or 79,artist or '',12,MUTED,570),
    label('mina-time',708,88,time(p)..' / '..time(duration),12,MUTED,112),
    ui.progress({key='mina-progress',x=origin+124*s,y=top+108*s,width=696*s,height=2*s,progress=ratio,
      fill=ACCENT,track='#63404B',radius=1*s}),
  }
  if folds then
    table.insert(nodes,6,ui.graph({key='mina-lining',x=origin+6*s,y=top+109*s,width=848*s,height=32*s,
      values=folds,values2=lining,color='#BD4C6A',color2='#783149CC',lineWidth=1.2*s,fillOpacity=0.65}))
  end
  if album then table.insert(nodes,label('mina-album',124,88,album,11,MUTED,560)) end
  if available() and music.artPath then
    table.insert(nodes,ui.image({key='mina-cover',path=music.artPath,x=origin+40*s,y=top+39*s,width=64*s,height=64*s,
      fit='cover',radius=2*s,border='#9F7863',borderWidth=0.7*s}))
  else
    table.insert(nodes,ui.glyph({key='mina-empty-cover',name='music',x=origin+58*s,y=top+57*s,size=28*s,color=MUTED}))
  end
  barWidget.render(ui.box({key='mina-music',width=width,height=height,fill='#00000000',clip=false,
    inputRects={0,0,0,0}},nodes))
end
local function refresh(value)
  envelope = status.envelope(value)
  local track = tostring(envelope.music.trackId or envelope.music.title or '')
  if track ~= lastTrack or not playing() then spectrum:clear() end
  lastTrack = track
  spectrum:step(noctalia.nowMs(),playing())
  draw()
end
function onBarGeometry(w,h)
  width,height=math.max(1,w),math.max(1,h)
  draw()
end
function onAudioSpectrum(csv)
  local now=noctalia.nowMs()
  spectrum:accept(csv,now,playing())
  if now-lastDraw < 30 then return end
  spectrum:step(now,playing())
  draw()
end
function update()
  local now = noctalia.nowMs()
  spectrum:step(now,playing())
  if now-lastDraw >= 30 then draw() end
end
noctalia.setUpdateInterval(100)
noctalia.state.watch('skin.status',refresh)
refresh(noctalia.state.get('skin.status'))
