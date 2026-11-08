--!nonstrict
local C = {}
C.asset = 'skin-assets/ai-cassette/'
C.text = '#EAE5DC'
C.muted = '#B8B4AE'
C.brass = '#D5B78C'
C.good = '#B6C8B5'
C.bad = '#E1A5A9'
C.rule = '#45464B'
C.selection = '#3C2029'
C.names = {'Лимиты','Архив','Бот','Логи','Память'}
function C.label(key, text, props)
  props = props and table.clone(props) or {}
  props.key, props.text = key, tostring(text or '')
  props.fontFamily = props.fontFamily or 'Noto Sans'
  props.fontSize = props.fontSize or 13
  props.color = props.color or C.text
  return ui.label(props)
end
function C.ruleNode(key)
  return ui.separator({key=key,color=C.rule,thickness=1})
end
function C.button(key, text, fn, props)
  props = props or {}
  return ui.button({key=key,text=text,onClick=fn,width=props.width or 120,height=34,fontSize=13,
    enabled=props.enabled~=false,tooltip=props.tooltip or '',variant='ghost',radius=3,
    selected=props.selected==true,fill='#191A1D',color=props.color or C.text,
    border='#817D78',borderWidth=1,hoverFill='#2D2A2C',pressedFill=C.selection,
    selectedFill=C.selection,selectedColor=C.text})
end
function C.well(key, children, props)
  props = props and table.clone(props) or {}
  props.key, props.gap, props.padding = key, props.gap or 10, props.padding or 0
  props.fill = '#00000000'
  return ui.column(props,children)
end
function C.detail(key, children, props)
  props = props and table.clone(props) or {}
  props.key, props.gap, props.padding = key, props.gap or 10, props.padding or 14
  props.fill, props.radius = '#191A1D',3
  return ui.column(props,children)
end
function C.log(key, lines, height)
  local nodes = {}
  for i,line in ipairs(lines) do
    local warning = string.sub(line,1,#'⚠')=='⚠' or string.sub(line,1,#'−')=='−'
    table.insert(nodes,C.label(key..'-line-'..i,line,{fontSize=12,fontFamily='Noto Sans Mono',
      maxLines=1,color=warning and C.bad or C.muted}))
  end
  if #nodes==0 then table.insert(nodes,C.label(key..'-empty','Лога пока нет',{color=C.muted})) end
  return ui.scroll({key=key,height=height,flexGrow=height and 0 or 1,gap=4,padding=12,
    fill='#0D0E10',stickToBottom=true},nodes)
end
function C.empty(key,text)
  return C.detail(key,{C.label(key..'-text',text,{color=C.muted,maxLines=4})},{padding=16})
end
function C.frame(geometry,header,content,freshness,menu)
  local w,h = geometry.width,geometry.height
  local children = {
    ui.image({key='cape-material',path=C.asset..'cape.png',width=w,height=h,fit='stretch'}),
  }
  table.insert(children,ui.column({key='cassette-content',x=48,y=88,width=math.max(1,w-120),height=48}, {header}))
  table.insert(children,ui.column({key='content-viewport',x=48,y=156,
    width=math.max(1,w-120),height=math.max(1,h-236)},{
      ui.row({height=math.max(1,h-236),flexGrow=1},{
        ui.scroll({key='content-scroll',flexGrow=1,gap=0,padding=0},{content}),
      }),
  }))
  table.insert(children,C.label('freshness',freshness,{x=math.max(48,w-360),y=h-70,width=288,
    height=20,fontSize=11,color=C.muted,maxLines=1,textAlign='end'}))
  if menu then
    table.insert(children,ui.column({key='section-menu',x=48,y=140,width=232,padding=8,gap=4,
      fill='#222024',border='#817D78',borderWidth=1,radius=3},menu))
  end
  return ui.box({key='ai-cassette',width=w,height=h,fill='#00000000',clip=true},children)
end
return C
