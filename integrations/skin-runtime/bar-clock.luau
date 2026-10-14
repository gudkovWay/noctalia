--!nonstrict
-- Independent clock after the AI slot; never merged into the monitoring hitbox.
local skin = require('./desktop_skin.luau')
local signature = ''
local function pattern(raw, fallback)
  local value = type(raw)=='string' and raw or fallback
  -- gsub's replacement count must not become formatTime's timestamp argument.
  local cleaned = value:gsub('^{:', ''):gsub('}$', '')
  return cleaned
end
function update()
  if not skin.kumihimo() or barWidget.isVertical() then
    barWidget.render(ui.box({key='bar-clock-hidden',width=0,height=0,visible=false}))
    signature=''
    return
  end
  local time = noctalia.formatTime(pattern(noctalia.timeFormat(), '%H:%M'))
  local date = noctalia.formatTime('%d.%m')
  local nextSignature = date..' '..time
  if signature==nextSignature then return end
  signature=nextSignature
  barWidget.render(ui.column({key='bar-clock',width=92,height=56,gap=2,paddingV=10,align='center'}, {
    ui.label({key='bar-clock-date',text=date,fontSize=11,fontFamily='Noto Sans',color='#B5A38F',maxLines=1}),
    ui.label({key='bar-clock-time',text=time,fontSize=14,fontFamily='Noto Sans',color='#E7D7BC',maxLines=1}),
  }))
  barWidget.setTooltip({{key='Дата',value=noctalia.formatTime(pattern(noctalia.dateFormat(), '%A, %x'))}})
end
noctalia.setUpdateInterval(1000)
update()
