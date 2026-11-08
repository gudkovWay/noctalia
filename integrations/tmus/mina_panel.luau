--!nonstrict
local M = {width=1180,height=760,active=false,menu=nil,confirm=nil,eqDraft={}}
local skin = require('./desktop_skin.luau')
local C = {text='#DDD4D3',muted='#B4AAAD',accent='#CCA99B',rule='#54464A',red='#AA3E56'}
local clear = '#00000000'
local names = {{'home','Главная'},{'search','Поиск'},{'library','Библиотека'},{'clients','Источники'},{'eq','Эквалайзер'}}
local VIEW_HEADING_HEIGHT, VIEW_GAP = 38, 8
local HOME_SHELF_HEIGHT, HOME_SHELF_GAP = 202, 12
local SEARCH_LINE_HEIGHT, SEARCH_FILTER_HEIGHT, RULE_THICKNESS = 36, 28, 1

function M.enabled()
  return noctalia.getSetting('bar.mina-left.enabled') == true
end
function M.label(key,text,props)
  props = props and table.clone(props) or {}
  props.key,props.text = key,tostring(text or '')
  props.fontFamily,props.fontSize = skin.FONT,props.fontSize or 13
  props.color,props.maxLines = props.color or C.text,props.maxLines or 1
  return ui.label(props)
end
function M.rule(key,hot)
  return ui.separator({key=key,color=hot and C.accent or C.rule,thickness=RULE_THICKNESS})
end
function M.button(key,text,fn,props)
  props = props or {}
  return ui.button({key=key,text=text,onClick=fn,height=props.height or 32,width=props.width,
    flexGrow=props.flexGrow,enabled=props.enabled~=false,variant='ghost',radius=0,
    fontFamily=skin.FONT,fontSize=props.fontSize or 13,color=props.active and C.accent or C.muted,
    fill=clear,hoverFill='#30232B88',pressedFill='#54333D99',border=clear,borderWidth=0,
    hintIdentity=props.identity or key,tooltip=props.tooltip or text})
end
function M.art(key,path,size)
  if type(path)=='string' and path~='' then
    return ui.image({key=key,path=path,width=size,height=size,fit='cover'})
  end
  return ui.box({key=key,width=size,height=size,fill=clear,border=C.rule,borderWidth=1},{
    ui.glyph({key=key..'-empty',name='music',x=(size-20)/2,y=(size-20)/2,size=20,color=C.muted}),
  })
end
function M.layout(view)
  local bodyWidth=math.max(300,M.width-166)
  local bodyHeight=math.max(240,M.height-294)
  local narrow=M.width<1050
  local queueWidth=narrow and bodyWidth or 230
  local mainWidth=narrow and bodyWidth or bodyWidth-queueWidth-28
  local controlsHeight=view=='search'
    and SEARCH_LINE_HEIGHT+RULE_THICKNESS+2*SEARCH_FILTER_HEIGHT+4*VIEW_GAP or 88
  return {width=bodyWidth,height=bodyHeight,main=mainWidth,queue=queueWidth,narrow=narrow,
    tracks=math.max(1,math.min(6,math.floor((bodyHeight-controlsHeight-80)/44))),
    queueRows=math.max(1,math.floor((bodyHeight-270)/48)),
    homeRows=math.max(1,math.floor((bodyHeight-VIEW_HEADING_HEIGHT-VIEW_GAP+HOME_SHELF_GAP)
      /(HOME_SHELF_HEIGHT+HOME_SHELF_GAP)))}
end
function M.track(model,row,index)
  local key='mina-track-'..index
  local chosen=index==model.cursor
  local playing=row.id==tostring(model.snapshot.trackId or '')
  local identity=model.view..':'..model.playlist.id..':'..index..':'..tostring(row.id or row.open or '')
  local metadata=row.subtitle or ''
  local provider=model.provider(row)
  if provider~='' then metadata=metadata..' · '..provider end
  local content={}
  if chosen and row.kind=='track' then
    content={
      M.art(key..'-art',row.art,92),
      ui.column({key='mina-expanded-'..index,gap=7,flexGrow=1},{
        M.label(key..'-title',row.title,{fontSize=23}),
        M.label(key..'-sub',metadata..(row.time and ' · '..row.time or ''),{color=C.muted,fontSize=12}),
        ui.row({key=key..'-actions',gap=4,height=34},{
          M.button('mina-track-play-'..index,'Слушать',function() model.activate(row,index) end,{width=100,identity=identity..':play'}),
          M.button('mina-track-radio-'..index,'Радио',function() model.trackAction('radio',row,index) end,{width=82,enabled=model.radio(row),identity=identity..':radio'}),
          M.button('mina-track-add-'..index,'В плейлист…',function() model.trackAction('add',row,index) end,{width=132,identity=identity..':add'}),
          M.button('mina-track-like-'..index,row.rating=='liked' and 'Нравится ✓' or 'Нравится',function() model.trackAction('like',row,index) end,{width=112,identity=identity..':like'}),
        }),
      }),
    }
  else
    content={
      M.label(key..'-index',playing and '▶' or tostring(index),{width=22,color=playing and C.accent or C.muted,fontSize=12}),
      M.art(key..'-art',row.art,34),
      ui.column({key=key..'-text',flexGrow=1,gap=1},{
        M.label(key..'-title',row.title,{fontSize=15}),
        M.label(key..'-sub',metadata,{fontSize=11,color=C.muted}),
      }),
      M.label(key..'-duration',row.time or '',{width=42,color=C.muted,fontSize=12}),
      M.button('mina-track-menu-'..index,'···',function() model.trackAction('menu',row,index) end,{width=32,identity=identity..':menu',tooltip='Действия: '..tostring(row.title)}),
    }
  end
  return ui.column({key=key..'-wrap',gap=0,height=chosen and row.kind=='track' and 124 or 44},{
    ui.row({key=key,align='center',gap=12,paddingH=6,paddingV=chosen and 10 or 3,
      height=chosen and row.kind=='track' and 123 or 43,fill=clear,
      hintIdentity=identity..':select',onClick=function() model.select(index) end},content),
    M.rule(key..'-rule',chosen and model.focus==2),
  })
end
function M.trackList(model)
  local nodes={}
  for n,row in ipairs(model.items) do
    local i=model.first+n-1
    if row.kind=='more' then
      nodes[#nodes+1]=M.label('mina-load-more',row.title,{color=C.muted})
    else
      nodes[#nodes+1]=M.track(model,row,i)
    end
  end
  return ui.column({key='mina-track-list',gap=0,flexGrow=1},nodes)
end
function M.library(model)
  local lines,line={},{}
  local cardWidth=math.floor((model.layout.main-24)/model.columns)
  for k,row in ipairs(model.items) do
    local i=model.first+k-1
    local key='mina-playlist-'..i
    line[#line+1]=ui.column({key=key,width=cardWidth,gap=5,padding=4,
      hintIdentity='library:'..i..':'..tostring(row.open),
      onClick=function() model.activate(row,i) end},{
      ui.row({height=94},{M.art(key..'-art',row.art,94)}),
      M.label(key..'-title',row.title,{fontSize=15}),
      M.label(key..'-sub',row.subtitle,{fontSize=11,color=C.muted}),
      M.rule(key..'-rule',i==model.cursor and model.focus==2),
    })
    if #line==model.columns or k==#model.items then
      lines[#lines+1]=ui.row({key='mina-playlists-'..#lines,gap=8,height=155},line)
      line={}
    end
  end
  return ui.column({key='mina-library-grid',gap=12},lines)
end
function M.home(model)
  local shelves={}
  for _,shelf in ipairs(model.home) do
    local cards={}
    local cardWidth=math.floor((model.layout.main-24)/3)
    for _,entry in ipairs(shelf.cards) do
      local key='mina-home-'..shelf.index..'-'..entry.index
      cards[#cards+1]=ui.column({key=key,width=cardWidth,gap=4,padding=3,
        hintIdentity=key..':'..tostring(entry.row.id),onClick=entry.select},{
        ui.row({height=84},{M.art(key..'-art',entry.row.art,84)}),
        M.label(key..'-title',entry.row.title,{fontSize=14}),
        M.label(key..'-artist',entry.row.subtitle,{fontSize=11,color=C.muted}),
        M.button(key..'-open',entry.row.kind=='track' and 'Слушать' or 'Открыть',entry.activate,{height=28}),
        M.rule(key..'-rule',entry.chosen and model.focus==2),
      })
    end
    shelves[#shelves+1]=ui.column({key='mina-shelf-'..shelf.index,height=HOME_SHELF_HEIGHT,gap=6},{
      M.label('mina-shelf-title-'..shelf.index,shelf.title,{fontSize=15}),
      ui.row({key='mina-shelf-cards-'..shelf.index,gap=8},cards),
    })
  end
  if #shelves==0 then shelves={M.label('mina-home-state',model.homeMessage,{color=C.muted,maxLines=3})} end
  return ui.column({key='mina-home',gap=HOME_SHELF_GAP},shelves)
end
function M.clients(model)
  local out={}
  local auth={Ready='Готов',Anonymous='Без входа',Missing='Нет сессии',Expired='Сессия истекла'}
  for k,row in ipairs(model.items) do
    local i=model.first+k-1
    if not row.restart then
      local meta=model.auth(row.payload)
      out[#out+1]=ui.column({key='mina-client-wrap-'..i,gap=4,height=66},{
        ui.row({key='mina-client-'..i,height=58,gap=12,align='center',
          hintIdentity='source:'..tostring(row.payload),onClick=function() model.activate(row,i) end},{
          M.label('mina-client-mark-'..i,row.active and '●' or '○',{width=20,color=row.active and C.accent or C.muted}),
          ui.column({flexGrow=1,gap=3},{M.label('mina-client-name-'..i,row.title,{fontSize=17}),
            M.label('mina-client-hint-'..i,meta and meta.authHint or row.subtitle,{fontSize=11,color=C.muted})}),
          M.label('mina-client-state-'..i,meta and (auth[meta.authKind] or 'Неизвестно') or 'Все источники',
            {width=132,fontSize=12,color=C.muted}),
        }),
        M.rule('mina-client-rule-'..i,i==model.cursor and model.focus==2),
      })
    end
  end
  return ui.column({key='mina-clients',gap=4},out)
end
function M.eq(model)
  local eq=model.snapshot.equalizer
  if type(eq)~='table' then return M.label('mina-eq-unavailable','Эквалайзер недоступен',{color=C.muted}) end
  local out={ui.row({key='mina-eq-controls',gap=12,height=36},{
    M.button('mina-eq-power',eq.enabled and 'Включён' or 'Выключен',function() model.command('eq',eq.enabled and 'off' or 'on') end,{width=120}),
    M.button('mina-eq-preset',tostring(eq.preset or 'Пресеты')..' ▾',model.presets,{flexGrow=1}),
    M.button('mina-eq-flat','Сброс · Flat',function() model.command('eq','flat') end,{width=132}),
  })}
  for i,gain in ipairs(eq.bands or {}) do
    local draft=M.eqDraft[i] or tonumber(gain) or 0
    out[#out+1]=ui.row({key='mina-eq-row-'..i,height=32,gap=12,align='center'},{
      M.label('mina-eq-freq-'..i,model.frequencies[i] or tostring(i),{width=48,color=C.muted,fontSize=12}),
      ui.slider({key='mina-eq-band-'..i,min=-15,max=15,step=1,value=draft,flexGrow=1,
        fill=C.red,track=C.rule,thumbColor=C.accent,focusColor=C.accent,
        onChange=function(v) M.eqDraft[i]=math.clamp(tonumber(v) or 0,-15,15); model.redraw() end,
        onDragEnd=function(v)
          local value=M.eqDraft[i] or math.clamp(tonumber(v) or draft,-15,15)
          M.eqDraft[i]=nil;model.command('eq','band:'..i..':'..tostring(value))
        end}),
      M.label('mina-eq-gain-'..i,string.format('%+g dB',draft),{width=56,fontSize=12,color=C.muted}),
    })
  end
  return ui.column({key='mina-eq',gap=3},out)
end
function M.volume(model)
  local value=model.volume
  local width=model.layout.queue
  local materialWidth=math.min(210,width)
  local left=math.floor((width-materialWidth)/2)
  return ui.column({key='music-volume',gap=0,height=78},{
    ui.row({key='music-volume-control',height=30,gap=8,align='center',
      onClick=model.volumeFocus,hintIdentity='music-volume-mode'}, {
      M.label('music-volume-title','Музыка',{color=C.muted,flexGrow=1}),
      M.label('music-volume-value',tostring(value)..'%',{fontSize=19,color=model.volumeKeys and C.accent or C.text}),
    }),
    ui.box({key='music-volume-strip',width=width,height=44,fill=clear},{
      ui.image({key='music-volume-empty',path='skin-assets/mina/volume-lining.png',x=left,y=8,width=materialWidth,height=28,fit='stretch'}),
      ui.box({key='music-volume-reveal',x=left,y=8,width=materialWidth*value/100,height=28,clip=true,fill=clear},{
        ui.image({key='music-volume-fill',path='skin-assets/mina/volume-lining-fill.png',width=materialWidth,height=28,fit='stretch'}),
      }),
      ui.slider({key='music-gain',x=0,y=0,width=width,height=44,min=0,max=100,step=5,value=value,
        enabled=model.snapshot.linked==true,fill=clear,track=clear,thumbColor=clear,focusColor=clear,
        onChange=model.volumeDraft,onDragEnd=model.volumeCommit}),
    }),
  })
end
function M.queue(model)
  local s=model.snapshot
  local now={M.label('mina-queue-title','Очередь',{fontSize=23}),
    M.button('mina-modes-menu','Режимы…',model.modes,{height=30})}
  if tostring(s.trackId or '')~='' then
    now[#now+1]=ui.row({key='mina-now',height=56,gap=10,align='center'},{
      M.art('mina-now-art',s.artPath,44),
      ui.column({flexGrow=1,gap=3},{M.label('mina-now-title',s.title,{fontSize=15}),
        M.label('mina-now-artist',s.artist,{color=C.muted,fontSize=11})}),
    })
    now[#now+1]=ui.row({height=30,gap=8,align='center'},{
      M.label('mina-now-time',model.time,{color=C.muted,flexGrow=1,fontSize=12}),
      M.button('mina-now-actions','Действия…',model.nowActions,{width=100,height=30}),
    })
  else
    now[#now+1]=M.label('mina-now-empty',s.linked and 'Нет текущего трека' or 'tmusd недоступен',{color=C.muted,maxLines=2})
  end
  now[#now+1]=M.volume(model)
  now[#now+1]=M.label('mina-queue-count','В очереди · '..tostring(#(s.queue or {})),{color=C.muted,fontSize=12})
  for _,entry in ipairs(model.queue) do
    local i=entry.index
    local key='mina-queue-'..i
    now[#now+1]=ui.column({key=key..'-wrap',height=48,gap=0},{
      ui.row({key=key,height=47,gap=8,align='center',hintIdentity='queue:'..i..':'..entry.id,
        onClick=entry.select},{
        M.label(key..'-index',entry.playing and '▶' or tostring(i),{width=14,fontSize=11,color=C.muted}),
        M.art(key..'-art',entry.row.art_path,32),
        ui.column({key=key..'-text',flexGrow=1,gap=2},{
          M.label(key..'-title',entry.row.title,{fontSize=13}),
          M.label(key..'-artist',entry.artist,{fontSize=10,color=C.muted}),
        }),
        M.button('mina-queue-play-'..i,'▷',entry.play,{width=28,height=32,identity='queue-play:'..i..':'..entry.id,tooltip='Слушать: '..tostring(entry.row.title)}),
      }),
      M.rule(key..'-rule',entry.chosen and model.focus==3),
    })
  end
  return ui.column({key='mina-queue-content',gap=5,flexGrow=1},now)
end
function M.overlay(model)
  if model.naming then
    return ui.column({key='mina-naming',x=56,y=146,width=model.layout.width,height=100,padding=16,gap=10,fill='#171317',border=C.rule,borderWidth=1},{
      M.label('mina-naming-title','Новый плейлист',{fontSize=20}),
      M.label('mina-naming-text',model.name..'▌',{fontSize=18}),
      M.label('mina-naming-help','Enter — создать · Esc — отмена',{fontSize=12,color=C.muted}),
    })
  end
  if M.confirm then
    return ui.column({key='mina-confirm',x=76,y=214,width=math.min(620,model.layout.width-40),padding=22,gap=16,
      fill='#171317',border=C.rule,borderWidth=1},{
      M.label('mina-confirm-title',M.confirm.title,{fontSize=20,maxLines=3}),
      ui.row({gap=12,height=36},{
        M.button('mina-confirm-cancel','Отмена',model.cancel,{width=140,active=not M.confirm.apply}),
        M.button('mina-confirm-apply','Подтвердить',model.confirm,{width=170,active=M.confirm.apply}),
      }),
    })
  end
  if M.menu then
    local nodes={M.label('mina-menu-title',M.menu.title,{fontSize=18})}
    local count=math.max(1,math.floor((model.layout.height-72)/39))
    M.menu.top=math.clamp(M.menu.top or 1,math.max(1,M.menu.cursor-count+1),M.menu.cursor)
    for i=M.menu.top,math.min(#M.menu.entries,M.menu.top+count-1) do
      local entry=M.menu.entries[i]
      nodes[#nodes+1]=M.button('mina-menu-'..entry.key,entry.title,entry.fn,{height=34,
        active=i==M.menu.cursor,enabled=entry.enabled~=false,identity=M.menu.identity..':'..entry.key})
    end
    return ui.column({key='mina-menu',x=math.max(56,M.width-452),y=146,width=320,padding=16,gap=5,
      fill='#171317',border=C.rule,borderWidth=1},nodes)
  end
  return nil
end
function M.render(model)
  local layout=model.layout
  local tabs={}
  for _,entry in ipairs(names) do
    local id,title=entry[1],entry[2]
    tabs[#tabs+1]=ui.column({key='mina-tab-wrap-'..id,gap=0,flexGrow=1},{
      M.button('mina-tab-'..id,title,function() model.section(id) end,{height=34,fontSize=18,active=model.sectionId==id}),
      M.rule('mina-tab-line-'..id,model.sectionId==id),
    })
  end
  local main={}
  if model.view=='search' then
    main[#main+1]=ui.row({key='mina-search-line',height=SEARCH_LINE_HEIGHT,gap=12,align='center',onClick=model.searchFocus}, {
      model.searchText,M.button('mina-search-submit','Найти',model.searchSubmit,{width=90}),
    })
    main[#main+1]=M.rule('mina-search-rule',model.typing)
    local kinds,providers={},{}
    for _,item in ipairs(model.kinds) do kinds[#kinds+1]=M.button('mina-kind-'..item.id,item.title,item.fn,{height=SEARCH_FILTER_HEIGHT,flexGrow=1,active=item.active}) end
    for _,item in ipairs(model.providers) do providers[#providers+1]=M.button('mina-provider-'..item.id,item.title,item.fn,{height=SEARCH_FILTER_HEIGHT,flexGrow=1,active=item.active}) end
    main[#main+1]=ui.row({height=SEARCH_FILTER_HEIGHT,gap=4},kinds)
    main[#main+1]=ui.row({height=SEARCH_FILTER_HEIGHT,gap=4},providers)
  else
    local heading=model.view=='playlist' and model.playlist.title or ({home='Рекомендации',library='Плейлисты',clients='Источники',eq='Эквалайзер'})[model.view]
    main[#main+1]=ui.row({key='mina-view-heading',height=VIEW_HEADING_HEIGHT,gap=8,align='center'},{
      M.label('mina-view-title',heading,{fontSize=24,flexGrow=1}),
      M.button(model.view=='clients' and 'mina-service-menu' or 'mina-view-action',model.viewActionTitle,model.viewAction,{width=142}),
    })
    if model.view=='playlist' then
      main[#main+1]=ui.row({key='mina-playlist-actions',height=30,gap=12},{
        M.button('mina-playlist-back','‹ Плейлисты',model.back,{width=136}),
        M.button('mina-playlist-listen','Слушать',model.playPlaylist,{width=108}),
        M.label('mina-playlist-count',tostring(model.total)..' треков',{color=C.muted,fontSize=12}),
      })
    end
  end
  local body
  if model.help then body=model.help
  elseif model.picker then body=model.picker
  elseif model.message~='' then body=M.label('mina-state',model.message,{color=C.muted,maxLines=5,fontSize=15})
  elseif model.view=='home' then body=M.home(model)
  elseif model.view=='library' then body=M.library(model)
  elseif model.view=='clients' then body=M.clients(model)
  elseif model.view=='eq' then body=M.eq(model)
  else body=M.trackList(model) end
  if model.error~='' then main[#main+1]=M.label('mina-error',model.error,{fontSize=12,color='#DFA4AA',maxLines=2}) end
  main[#main+1]=ui.scroll({key='mina-view-scroll',flexGrow=1,padding=0,gap=0},{body})
  local children={ui.image({key='music-cape',path='skin-assets/mina/cape.png',width=M.width,height=M.height,fit='stretch'}),
    ui.row({key='mina-tabs',x=56,y=92,width=layout.width,height=36,gap=4},tabs)}
  if not layout.narrow or model.focus~=3 then
    children[#children+1]=ui.column({key='mina-main-pane',x=56,y=146,width=layout.main,height=layout.height,gap=VIEW_GAP},main)
  end
  if not layout.narrow or model.focus==3 then
    children[#children+1]=ui.column({key='mina-queue-pane',x=layout.narrow and 56 or 56+layout.main+28,y=146,
      width=layout.queue,height=layout.height,padding=0,gap=0},{M.queue(model)})
  end
  children[#children+1]=ui.row({key='mina-footer',x=56,y=M.height-142,width=layout.width,height=28,gap=12,align='center'},{
    M.label('mina-keymap',model.volumeKeys and 'h/l · ←/→ — громкость · Esc — выход' or 'F — действия · ? — карта · Tab — буферы',
      {fontSize=11,color=C.muted,flexGrow=1}),
    M.button('mina-buffer-toggle',model.focus==3 and '‹ Каталог' or 'Очередь ›',model.toggleBuffer,{width=110,height=28}),
    M.button('mina-close','Закрыть',model.close,{width=90,height=28}),
  })
  local overlay=M.overlay(model)
  if overlay then
    local function inhibit(node)
      if node.props then
        node.props.onClick,node.props.onChange,node.props.onDragEnd=nil,nil,nil
        node.props.enabled=false
      end
      for _,child in ipairs(node.children or {}) do inhibit(child) end
    end
    for _,node in ipairs(children) do inhibit(node) end
  end
  if overlay then children[#children+1]=overlay end
  return ui.box({key='mina-music-panel',width=M.width,height=M.height,fill=clear,clip=true},children)
end
return M
