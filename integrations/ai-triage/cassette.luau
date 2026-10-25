--!nonstrict
-- One material/layout vocabulary for all five AI tabs. Text remains native.
local skin = require('./desktop_skin.luau')
local C = {}
C.asset = 'skin-assets/ai-cassette/'
C.text = '#E7D7BC'
C.muted = '#B5A38F'
C.brass = '#D4AF70'
C.good = '#A7B9A1'
C.bad = '#D39A91'
C.glass = '#24343B80'
C.rule = '#9BADB12B'
C.paper = '#CEC2A8'
C.ink = '#302D28'
C.names = {'Лимиты','Архив','Бот','Логи','Память'}
function C.label(key, text, props)
  props = props and table.clone(props) or {}
  props.key, props.text = key, tostring(text or '')
  props.fontFamily = props.fontFamily or 'ComicShannsMono Nerd Font'
  props.fontSize = props.fontSize or 13
  props.color = props.color or C.text
  return ui.label(props)
end
function C.ruleNode(key)
  return ui.separator({key=key, color=C.rule, thickness=1})
end
function C.button(key, text, fn, props)
  props = props or {}
  local w = props.width or 120
  local enabled = props.enabled ~= false
  return ui.box({key=key..'-seat',width=w,height=30,fill='#00000000'}, {
    ui.image({key=key..'-metal',path=C.asset..'control.png',width=w,height=30,fit='stretch',opacity=enabled and 1 or 0.4}),
    ui.button({key=key,text=text,onClick=fn,width=w,height=30,fontSize=12,
      enabled=enabled,tooltip=props.tooltip or '',variant='ghost',radius=2,
      fill='#00000000',color=props.color or C.text,border='#8A7157B0',borderWidth=0.7,
      hoverFill='#A480514D',pressedFill='#B28A6073'}),
  })
end
function C.well(key, children, props)
  props = props or {}
  props.key, props.gap, props.padding = key, props.gap or 8, props.padding or 12
  props.fill, props.border, props.borderWidth, props.radius = C.glass, C.rule, 0.7, 1
  return ui.column(props, children)
end
function C.paperWell(key, children, props)
  props = props or {}
  props.key, props.gap, props.padding = key, props.gap or 8, props.padding or 14
  props.fill, props.border, props.borderWidth, props.radius = C.paper, '#A28D69', 1, 1
  return ui.column(props, children)
end
function C.log(key, lines, height)
  local nodes = {}
  for i, line in ipairs(lines) do
    local warning = string.sub(line,1,#'⚠') == '⚠' or string.sub(line,1,#'−') == '−'
    table.insert(nodes,C.label(key..'-line-'..i,line,{fontSize=12,fontFamily='ComicShannsMono Nerd Font',
      maxLines=1,color=warning and C.bad or '#BCCBD0'}))
  end
  if #nodes == 0 then table.insert(nodes,C.label(key..'-empty','Лога пока нет',{color=C.muted})) end
  return ui.scroll({key=key,height=height,flexGrow=height and 0 or 1,gap=3,padding=10,
    fill='#0F191DC0',border=C.rule,borderWidth=1,stickToBottom=true},nodes)
end
function C.empty(key,text)
  return C.well(key,{C.label(key..'-text',text,{color=C.muted})},{flexGrow=1,justify='center'})
end
function C.frame(geometry, header, tabs, content, freshness, help, paged)
  local w,h = geometry.width,geometry.height
  local mounted = skin.kumihimo() and geometry.edge == 'top' and geometry.anchor >= 0
  local neck = mounted and 32 or 0
  local bodyH = h-neck
  local children = {}
  if skin.kumihimo() then
    table.insert(children,ui.image({key='cassette-body',path=C.asset..'body.png',y=neck,width=w,height=bodyH,fit='stretch'}))
  end
  table.insert(children,ui.column({key='cassette-content',x=32,y=neck+29,width=math.max(1,w-64),
    height=80,gap=8,fill='#00000000'}, {
    header,C.ruleNode('header-rule'),tabs,C.ruleNode('tab-rule'),
  }))
  table.insert(children,ui.column({key='content-viewport',x=32,y=neck+129,
    width=math.max(1,w-64),height=math.max(1,bodyH-249),gap=0},{paged and content or ui.scroll({key='content-scroll',flexGrow=1,gap=0,padding=0},{content})}))
  table.insert(children,ui.column({key='cassette-footer',x=32,y=neck+bodyH-105,
    width=math.max(1,w-64),height=80,gap=8,fill='#00000000'}, {
    C.ruleNode('footer-rule'),
    C.label('freshness',freshness,{fontSize=11,color=C.muted,maxLines=1}),
    C.label('action-help',help,{fontSize=11,color=C.muted,maxLines=1}),
    C.label('navigation-help','1–5 вкладки · h/l ←/→ · j/k ↑/↓ · g начало/конец · Enter раскрыть/свернуть · Esc закрыть · EN/RU',
      {fontSize=11,color=C.muted,maxLines=1}),
  }))
  if mounted then
    -- Host reports the opener relative to the actual clamped panel origin.
    -- The saddle overlays the same top rail, never the middle of the content.
    table.insert(children,ui.image({key='cassette-mount',path=C.asset..'mount.png',
      x=geometry.anchor-42,y=0,width=84,height=47,fit='stretch'}))
  end
  return ui.box({key='ai-cassette',width=w,height=h,fill=skin.kumihimo() and '#00000000' or 'surface',clip=true},children)
end
return C
