--!nonstrict
-- Native cassette presentation; the existing watch service remains the sole
-- publisher of AI state. Backend tools retain their own business rules.
local C = require('./cassette.luau')
local KEY, SERVICE = 'ai_triage', 'q/ai-triage:watch'
local AI, BOT = '/home/q/storage/ai', '/home/q/dev/chat-bot'
local TABS = {'лимиты','разбор','бот','логи','память'}
local HELP = {
 ['лимиты']='r запросить провайдеров заново · o полный отчёт OMP',
 ['разбор']='y/n вердикт · o редактор · Claude: два нажатия, платно · разрешение наружу — отдельно',
 ['бот']='s пуск · x стоп · m модель вкл. · u выгрузить · o журнал',
 ['логи']='Enter хвост сессии · o открыть полный лог в less',
 ['память']='y принять заметку / открыть сессию · n отклонить · o редактор',
}
local geometry = {width=900,height=732,anchor=-1,edge=''}
local st, rows, runs, memoryRows, live, preview = {}, {}, {}, {}, {}, {}
local tab, filter, expanded, confirm = 'лимиты', 'new', nil, nil
local sel, top = {}, {}
for _,name in ipairs(TABS) do sel[name],top[name]=1,1 end
local opened,busy,epoch,lastSignature = false,false,0,''
local errors = {['разбор']='',['логи']='',['память']='',memory='',action=''}
local PAGE = 11
local render
local function quoted(s) return "'"..string.gsub(tostring(s or ''),"'","'\\''").."'" end
local function poke(event) noctalia.runAsync('noctalia msg plugin '..SERVICE..' all '..event) end
local function bot() return st.bot end
local function daemon() return (bot() and bot()['демон']) or {} end
local function mdl() return (bot() and bot()['модель']) or {} end
local function alive() return daemon()['состояние']=='active' end
local function acting() return (st.acting or '')~='' or (st.actingModel or '')~='' end
local function why(res,fallback)
  local text = res and noctalia.string.trim(res.stderr or '') or ''
  return text~='' and text or fallback
end
local MATCH = {
 new=function(r) return r.fresh end, all=function() return true end,
 unsure=function(r) return r.verdict=='?' and not r.fresh end,
 warn=function(r) return r.share=='warning' end,
}
local function cards()
 local out={}
 for _,r in ipairs(rows) do if MATCH[filter](r) then table.insert(out,r) end end
 return out
end
local function items()
 if tab=='разбор' then return cards() elseif tab=='логи' then return runs
 elseif tab=='память' then return memoryRows end
 return {}
end
local function current() return items()[sel[tab]] end
local function idOf(r) return r and (r['путь'] or r.path or r.candidate_id or r.id) end
local function clamp()
 local n=#items()
 sel[tab]=math.clamp(sel[tab],1,math.max(1,n))
 top[tab]=math.clamp(top[tab],math.max(1,sel[tab]-PAGE+1),sel[tab])
end
local function readAsync(cmd,fn,timeout)
 local generation=epoch
 noctalia.runAsync(cmd,function(res)
  if opened and epoch==generation then fn(res) end
 end,timeout or 20000)
end
local function loadCards()
 readAsync(AI..'/bin/triage.py rows --json',function(res)
  local got=res and res.exitCode==0 and noctalia.json.decode(res.stdout or '')
  errors['разбор']=type(got)=='table' and '' or why(res,'Карточки не читаются')
  if type(got)=='table' then
   rows=got
   if filter=='new' and #cards()==0 then filter='all' end
  end
  clamp(); render()
 end)
end
local function loadRuns()
 readAsync(BOT..'/tools/runs.py --json',function(res)
  local got=res and res.exitCode==0 and noctalia.json.decode(res.stdout or '')
  errors['логи']=type(got)=='table' and '' or why(res,'Сессии не читаются')
  if type(got)=='table' then runs=got end
  clamp(); render()
 end)
end
local function loadMemory()
 readAsync({'python3',AI..'/bin/memory_write.py','rows','--status','disputed'},function(res)
  local got=res and res.exitCode==0 and noctalia.json.decode(res.stdout or '')
  errors['память']=type(got)=='table' and '' or why(res,'Кандидаты не читаются')
  if type(got)=='table' then memoryRows=got end
  clamp(); render()
 end)
end
local function linesOf(text,limit)
 local out={}
 for line in string.gmatch(text or '', '[^\n]+') do
  if #out>=limit then table.insert(out,'…'); break end
  table.insert(out,line)
 end
 return out
end
local function loadLive()
 local run=bot() and bot()['сессия']
 if not run or not run['путь'] then live={}; return end
 local path=run['путь']
 readAsync('tail -n 300 '..quoted(path),function(res)
  local active=bot() and bot()['сессия']
  if not active or active['путь']~=path then return end
  live=linesOf(res and res.exitCode==0 and res.stdout or why(res,'Лог не читается'),300)
  render()
 end)
end
local function loadPreview(r)
 local id=idOf(r)
 preview={}
 if tab=='логи' then
  readAsync('tail -n 24 '..quoted(r['путь']),function(res)
   if tab~='логи' or expanded~=id then return end
   preview=linesOf(res and res.exitCode==0 and res.stdout or why(res,'Лог не читается'),24)
   render()
  end)
 elseif tab=='память' then
  preview=linesOf(r.body or r.preview or '',28)
 else
  local text=noctalia.readFile(r.path or '')
  if not text then preview={'Карточка не читается'}; return end
  preview=linesOf(string.match(text,'^%-%-%-.-\n%-%-%-\n(.*)$') or text,28)
 end
end
local function toggle(r)
 if expanded==idOf(r) then expanded,preview=nil,{}
 else expanded=idOf(r); loadPreview(r) end
 render()
end
local function refresh()
 if tab=='лимиты' then poke('usage-invalidate'); return end
 poke('refresh')
 if tab=='разбор' then loadCards() elseif tab=='логи' then loadRuns()
 elseif tab=='память' then loadMemory() else loadLive() end
end
local function switch(name)
 if tab==name then if name=='лимиты' then poke('usage') end; return end
 tab=name; expanded,preview,confirm=nil,{},nil
 clamp(); render()
 if name=='лимиты' then poke('usage') elseif name=='разбор' then loadCards()
 elseif name=='логи' then loadRuns() elseif name=='память' then loadMemory() else loadLive() end
end
local function act(cmd)
 if busy or st.running then return end
 busy=true; confirm=nil; errors.action=''; render()
 readAsync(cmd,function(res)
  busy=false
  errors.action=res and res.exitCode==0 and '' or why(res,'Действие не выполнено')
  poke('refresh'); loadCards()
 end,30000)
end
local function flip(r,value) if r and r.path then act(AI..'/bin/triage.py flip '..quoted(r.path)..' --verdict '..value) end end
local function allow(r,value) if r and r.path then act(AI..'/bin/triage.py allow '..quoted(r.path)..' '..value) end end
local function openFull()
 local r=current()
 local cmd
 if tab=='лимиты' then cmd='env -u OMP_PROFILE -u PI_PROFILE PI_CODING_AGENT_DIR=/home/q/.omp/agent omp usage'
 elseif tab=='бот' then cmd='journalctl --user -u chat-bot -f'
 elseif r then
  local path=r.path or r.path_hint or r['путь']
  if path and path~='' then cmd=(tab=='логи' and 'less +G ' or 'nvim ')..quoted(path) end
 end
 if cmd then noctalia.runInTerminal(cmd); panel.close() end
end
local function resolveMemory(r,action)
 local id=r and (r.candidate_id or r.id)
 if busy or not id or id=='' then return end
 if action=='promote' and r.kind=='session' then openFull(); return end
 busy=true; errors.memory=''; render()
 readAsync({'python3',AI..'/bin/memory_write.py',action,id,'--reviewed-by','owner','--reason','resolved in Noctalia'},function(res)
  busy=false
  if not res or res.exitCode~=0 then
   errors.memory=why(res,'memory_write.py не ответил')
   noctalia.runAsync('notify-send -a ai -u critical -t 0 '..quoted('Память: действие не выполнено')..' '..quoted(errors.memory))
  end
  poke('memory-refresh'); loadMemory()
 end,30000)
end
local function countdown(at)
 if not at or at==0 then return '' end
 local seconds=math.floor((at-noctalia.nowMs())/1000)
 if seconds<=0 then return 'скоро' end
 if seconds>=86400 then return string.format('%dд %dч',math.floor(seconds/86400),math.floor(seconds%86400/3600)) end
 if seconds>=3600 then return string.format('%dч %dм',math.floor(seconds/3600),math.floor(seconds%3600/60)) end
 return string.format('%dм',math.floor(seconds/60))
end
local function age(at)
 return at and at>0 and ('обновлено '..math.max(0,math.floor((os.time()-at)/60))..' мин назад') or 'обновления ещё не было'
end
local function signature()
 local out={tab,age((st.usage or {}).at),age(st.at)}
 for _,p in ipairs((st.usage or {}).providers or {}) do
  for _,r in ipairs(p.rows or {}) do table.insert(out,countdown(r.resetsAt)) end
 end
 return table.concat(out,'|')
end
local function label(key,text,props) return C.label(key,text,props) end
local function notice(key,text,color) return label(key,text,{fontSize=12,color=color or C.bad,maxLines=3}) end
local function quotaProvider(p,i)
 local body={ui.row({gap=8,align='center'}, {
  label('provider-'..i,p.title or p.id or '?',{fontSize=16,fontWeight='semibold',flexGrow=1}),
  label('plan-'..i,p.plan or '',{fontSize=11,color=C.muted}),
 })}
 if (p.error or '')~='' then table.insert(body,notice('provider-error-'..i,p.error)) end
 for j,r in ipairs(p.rows or {}) do
  local f=tonumber(r.fraction) or -1
  local color=f>=0.8 and C.bad or f>=0.5 and C.brass or C.good
  local row={label('quota-'..i..'-'..j,(r.label or '?')..' · '..(r.text or '—'),{color=f<0 and C.muted or color})}
  if f>=0 then table.insert(row,ui.progress({key='meter-'..i..'-'..j,progress=math.clamp(f,0,1),
   height=6,radius=1,fill=color,track='#10181B'})) end
  table.insert(row,label('reset-'..i..'-'..j,countdown(r.resetsAt),{fontSize=11,color=C.muted}))
  table.insert(body,ui.column({gap=5},row))
 end
 if #(p.rows or {})==0 and (p.error or '')=='' then table.insert(body,notice('provider-empty-'..i,'Квота и сброс неизвестны',C.muted)) end
 return C.well('quota-provider-'..i,body)
end
local function usageTab()
 local u=st.usage or {}
 local quotas,balances={},{}
 for i,p in ipairs(u.providers or {}) do
  local monetary=p.id=='deepseek' or p.id=='vast'
  if not monetary and #(p.rows or {})>0 then
   monetary=true
   for _,r in ipairs(p.rows) do if (tonumber(r.fraction) or -1)>=0 then monetary=false; break end end
  end
  if monetary then
   local body={label('balance-'..i,p.title or p.id or '?',{fontSize=16,fontWeight='semibold'})}
   if (p.error or '')~='' then table.insert(body,notice('balance-error-'..i,p.error)) end
   for j,r in ipairs(p.rows or {}) do table.insert(body,label('balance-value-'..i..'-'..j,
    (r.label or 'Баланс')..' · '..(r.text or '—'),{fontSize=18})) end
   if #(p.rows or {})==0 then table.insert(body,label('balance-unknown-'..i,'Баланс неизвестен',{color=C.muted})) end
   table.insert(balances,C.well('money-'..i,body))
  else table.insert(quotas,quotaProvider(p,i)) end
 end
 if #quotas==0 then table.insert(quotas,C.empty('quotas-empty',u.busy and 'Считаю лимиты…' or 'Квот пока нет')) end
 if #balances==0 then table.insert(balances,C.empty('balances-empty','Денежных остатков пока нет')) end
 local body={}
 if u.busy then table.insert(body,notice('usage-busy','OMP обновляется · сохранён последний успешный снимок',C.brass)) end
 if (u.error or '')~='' then table.insert(body,notice('usage-error',u.error..' · показываю последний успешный снимок')) end
 table.insert(body,ui.row({key='usage-columns',gap=20,align='start'}, {
  ui.column({key='quota-column',width=math.floor((geometry.width-84)*0.67),gap=10},quotas),
  ui.column({key='balance-column',flexGrow=1,gap=10},{label('money-heading','ДЕНЕЖНЫЕ ОСТАТКИ',{fontSize=12,color=C.muted}),table.unpack(balances)}),
 }))
 return ui.column({key='limits',gap=10},body)
end
local function listPane(list)
 clamp()
 local out={}
 for i=top[tab],math.min(#list,top[tab]+PAGE-1) do
  local r=list[i]
  local chosen=i==sel[tab]
  local text=r.title or r['начало'] or r.id or '?'
  local small=tab=='логи' and ((r['исход'] or '?')..' · '..(r['длилась'] or '')..' · '..tostring(r['байт'] or 0)..' Б')
   or tab=='память' and (r.kind or 'note') or ((r.unread and '● ' or '')..(r.share=='warning' and 'наружу запрещено' or r.why or r.verdict or '?'))
  table.insert(out,ui.column({key='entry-'..i,height=34,gap=1,paddingH=8,paddingV=3,
   fill=chosen and '#9D79534D' or '#00000000',border=chosen and '#C28C5999' or '#00000000',borderWidth=1,
   onClick=function() sel[tab]=i; toggle(r) end}, {
   label('entry-title-'..i,text,{fontSize=12,maxLines=1}),
   label('entry-subtitle-'..i,small,{fontSize=10,maxLines=1,color=r.share=='warning' and C.bad or C.muted}),
  }))
 end
 if #list==0 then table.insert(out,C.empty('list-empty','Записей нет')) end
 table.insert(out,label('list-range',#list==0 and '0 записей' or string.format('%d–%d из %d',top[tab],math.min(#list,top[tab]+PAGE-1),#list),{fontSize=11,color=C.muted}))
 return C.well('list-pane',out,{width=math.floor((geometry.width-84)*0.34),padding=6,gap=2})
end
local function evidencePane(r)
 if not r then return C.empty('evidence','Выберите карточку') end
 local ink={color=C.ink,fontSize=12}
 local body={label('evidence-title',r.title or '?',{fontSize=16,color=C.ink,fontWeight='semibold'}),
  label('evidence-meta',(r.date or '')..' · '..(r.model or '')..' · вердикт '..(r.verdict or '?'),ink),
  label('evidence-reason',r.why or '',ink)}
 if r.share=='warning' then table.insert(body,label('share-warning','Наружу запрещено: '..(r.share_why or ''),{color='#713F38',fontSize=12,maxLines=3})) end
 local excerpt=expanded==idOf(r) and preview or {'Enter / щелчок — раскрыть содержимое карточки'}
 local lines={}
 for i,line in ipairs(excerpt) do table.insert(lines,label('evidence-line-'..i,line,{color=C.ink,fontSize=12,maxLines=3})) end
 table.insert(body,ui.scroll({key='evidence-scroll',height=260,gap=5},lines))
 return C.paperWell('evidence',body,{flexGrow=1})
end
local function triageTab()
 local working=busy or st.running==true
 local toolbar={C.button('run',st.running and 'Идёт разбор…' or (tonumber(st.new) or 0)>0 and ('Разобрать '..st.new..' новых') or 'Прогнать заново',
  function() if not working then poke('run') end end,{width=206,enabled=not working})}
 if (tonumber(st.unsure) or 0)>0 then table.insert(toolbar,C.button('escalate',confirm=='escalate' and 'Точно? Платно' or ('Спорные → Claude ('..st.unsure..')'),function()
  if working then return end
  if confirm=='escalate' then confirm=nil; poke('escalate') else confirm='escalate' end
  render()
 end,{width=226,enabled=not working,color=confirm=='escalate' and C.bad or C.text})) end
 table.insert(toolbar,C.button('triage-terminal','В терминале',function() noctalia.runInTerminal(AI..'/bin/ai triage review'); panel.close() end,{width=118}))
 local filters={}
 for _,entry in ipairs({{'new','Новое'},{'all','Все'},{'unsure','Спорное'},{'warn','Небезопасное'}}) do
  local id,title=entry[1],entry[2]; local n=0
  for _,r in ipairs(rows) do if MATCH[id](r) then n+=1 end end
  table.insert(filters,C.button('filter-'..id,title..' '..n,function() filter=id;sel[tab],top[tab]=1,1;expanded=nil;preview={};render() end,
   {width=id=='warn' and 145 or 108,color=filter==id and C.brass or C.muted}))
 end
 local r=current()
 local tools={C.button('verdict-yes','В индекс · y',function() flip(r,'да') end,{enabled=r~=nil and not working}),
  C.button('verdict-no','Не класть · n',function() flip(r,'нет') end,{enabled=r~=nil and not working}),
  C.button('evidence-open','Редактор · o',openFull,{enabled=r~=nil})}
 if r and r.share=='warning' and expanded==idOf(r) then
  table.insert(tools,C.button('share-approved','Можно наружу',function() allow(r,'approved') end,{width=132,enabled=not working}))
  table.insert(tools,C.button('share-never','Никогда',function() allow(r,'never') end,{width=96,enabled=not working}))
 end
 local body={ui.row({gap=8},toolbar),ui.row({gap=6},filters)}
 if errors['разбор']~='' then table.insert(body,notice('triage-error',errors['разбор'])) end
 if errors.action~='' then table.insert(body,notice('triage-action-error',errors.action)) end
 if #(st.lines or {})>0 then table.insert(body,C.log('pipeline',st.lines,72)) end
 table.insert(body,ui.row({key='triage-columns',gap=20,align='start'},{listPane(cards()),evidencePane(r)}))
 table.insert(body,ui.row({gap=6},tools))
 return ui.column({key='triage',gap=10},body)
end
local function botTab()
 local b=bot()
 if not b then return C.empty('bot-unknown','status.py не ответил · состояние демона и модели неизвестно') end
 local d,m=daemon(),mdl(); local chat,level=b['чат'] or {},b['уровень'] or {}
 local working=acting() or busy
 local function control(key,title,event,enabled,width)
  return C.button(key,title,function() if not acting() and not busy and enabled then poke(event) end end,{enabled=not working and enabled,width=width or 130})
 end
 local controls=ui.row({gap=8}, {
  control('bot-start','Пуск · s','bot-start',not alive() and d['есть']==true),
  control('bot-stop','Стоп · x','bot-stop',alive()),
  control('bot-restart','Перезапуск','bot-restart',d['есть']==true),
  control('model-on','Модель вкл. · m','model-on',m['вкл']==false or m['состояние']=='спит',148),
  control('model-off','Выгрузить · u','model-off',m['вкл']~=false,142),
 })
 local function fact(key,name,value,color)
  return ui.row({gap=12},{label(key..'-name',name,{width=132,color=C.muted,fontSize=12}),label(key,value,{flexGrow=1,color=color or C.text,fontSize=12})})
 end
 local facts={fact('daemon-state','демон',d['состояние'] or '?'),fact('uptime','время работы',d['с'] and (tostring(d['с'])..' с') or '—'),
  fact('model-state','модель',m['состояние'] or '?',
    m['состояние']=='спит' and C.bad or m['состояние']=='на связи' and C.good
    or (m['состояние']=='поднимается' or m['состояние']=='гаснет') and C.brass or C.muted),
  fact('search-state','поиск',(b['морда'] or {})['жива'] and 'на связи' or 'спит'),
  fact('index-state','индекс',(b['индекс'] or {})['жив'] and 'на связи' or 'спит'),
  fact('level','уровень',(level['имя'] or '?')..' · '..(level['почему'] or '')),
  fact('chat-count','чат',string.format('%d сообщений · своих %d · сегодня %d',chat['сообщений'] or 0,chat['своих'] or 0,chat['сегодня'] or 0)),
  fact('chat-last','последнее',chat['последнее'] and chat['последнее']~='' and chat['последнее'] or '—')}
 if m['вкл']==false then table.insert(facts,notice('manual-off','Модель погашена вручную — бот читает чат и молчит',C.muted)) end
 if d['есть'] and not d['автозапуск'] then table.insert(facts,notice('autostart-off','Автозапуск выключен',C.muted)) end
 local run=b['сессия']
 if run and run['исход']=='оборвалась' then table.insert(facts,notice('previous-run','Прошлый запуск оборвался: '..(run['хвост'] or ''))) end
 return ui.column({key='bot',gap=10},{controls,C.well('bot-facts',facts),
  ui.row({gap=8,align='center'},{label('live-heading',run and ('Живой лог · '..(run['файл'] or '')) or 'Лога нет',{flexGrow=1,color=C.muted}),C.button('journal','Журнал · o',openFull)}),
  C.log('live-log',live,220)})
end
local function logsTab()
 local r=current()
 local detail={}
 if r then
  table.insert(detail,label('session-path',r['путь'] or '',{fontSize=12,color=C.muted,maxLines=2}))
  table.insert(detail,label('session-outcome',r['исход'] or '?',{color=r['исход']=='оборвалась' and C.bad or C.good}))
  table.insert(detail,label('session-mode',(r['режим'] or '?')..' · '..(r['длилась'] or '—'),{fontSize=12}))
  table.insert(detail,label('session-start',(r['начало'] or '—')..' · '..tostring(r['байт'] or 0)..' Б',{fontSize=11,color=C.muted}))
  if (r['хвост'] or '')~='' then
   table.insert(detail,label('session-summary',r['хвост'],{fontSize=12,maxLines=3,
    color=r['исход']=='штатно' and C.muted or C.bad}))
  end
  table.insert(detail,C.log('log-tail',expanded==idOf(r) and preview or {'Enter / щелчок — прочитать хвост сессии'},330))
  table.insert(detail,C.button('log-open','Полный лог · o',openFull,{width=150}))
 else table.insert(detail,C.empty('no-session','Выберите сессию')) end
 local body={}
 if errors['логи']~='' then table.insert(body,notice('logs-error',errors['логи'])) end
 table.insert(body,ui.row({key='logs-columns',gap=20,align='start'},{listPane(runs),C.well('session-detail',detail,{flexGrow=1})}))
 return ui.column({key='logs',gap=10},body)
end
local function memoryTab()
 local r=current()
 local detail={}
 if r then
  table.insert(detail,label('memory-title',r.title or r.id or '?',{fontSize=16,fontWeight='semibold',color=C.ink}))
  table.insert(detail,label('memory-reviewer','Проверил: '..tostring(r.reviewed_by or r.reviewer or '—'),{color=C.ink,fontSize=12}))
  table.insert(detail,label('memory-confidence','Уверенность: '..tostring(r.review_confidence or '—'),{color=C.ink,fontSize=12}))
  table.insert(detail,label('memory-reason',r.reason or r.review_reason or '',{color=C.ink,fontSize=12,maxLines=4}))
  local text={}
  for i,line in ipairs(expanded==idOf(r) and preview or linesOf(r.body or r.preview or '',28)) do
   table.insert(text,label('memory-line-'..i,line,{color=C.ink,fontSize=12,maxLines=3}))
  end
  table.insert(detail,ui.scroll({key='memory-evidence',height=248,gap=5},text))
 else table.insert(detail,C.empty('memory-empty','Спорных кандидатов нет')) end
 local body={ui.row({gap=12,align='center'}, {
  label('memory-counts',string.format('ждут %d · спорных %d · принято %d · отклонено %d · всего %d',st.memory_pending or 0,st.memory_disputed or 0,st.memory_promoted or 0,st.memory_rejected or 0,st.memory_total or 0),{flexGrow=1,fontSize=12,color=C.muted}),
  C.button('memory-review',st.memory_review_running and 'Luna проверяет…' or 'Luna review',function() if not st.memory_review_running then poke('memory-review') end end,{width=144,enabled=not st.memory_review_running}),
 })}
 if errors['память']~='' then table.insert(body,notice('memory-load-error',errors['память'])) end
 if errors.memory~='' then table.insert(body,notice('memory-error',errors.memory)) end
 table.insert(body,ui.row({key='memory-columns',gap=20,align='start'},{listPane(memoryRows),C.paperWell('memory-detail',detail,{flexGrow=1})}))
 table.insert(body,ui.row({gap=8},{C.button('memory-accept',r and r.kind=='session' and 'Изменить / материализовать · y' or 'Принять заметку · y',function() resolveMemory(r,'promote') end,{width=248,enabled=r~=nil and not busy}),
  C.button('memory-reject','Отклонить · n',function() resolveMemory(r,'reject') end,{width=144,enabled=r~=nil and not busy}),C.button('memory-open','Редактор · o',openFull,{enabled=r~=nil})}))
 return ui.column({key='memory',gap=10},body)
end
render=function()
 if not opened then return end
 PAGE=math.clamp(math.floor((geometry.height-340)/34),1,11)
 clamp()
 local m=mdl()
 local header=ui.row({key='header',height=30,gap=8,align='center'}, {
  ui.image({key='neural-header',path=C.asset..'neural.png',width=26,height=26,fit='contain'}),
  label('title','Мираторг',{fontSize=17,fontWeight='semibold',width=146}),
  label('summary',bot() and ('демон '..(daemon()['состояние'] or '?')..' · модель '..(m['состояние'] or '?')) or 'состояние пока неизвестно',{fontSize=12,color=C.muted,flexGrow=1,maxLines=1}),
  C.button('refresh','↻ Сводка',refresh,{width=114}),C.button('close','↑ Свернуть',function() panel.close() end,{width=104}),
 })
 local tabs={}
 for i,name in ipairs(TABS) do
  table.insert(tabs,C.button('tab-'..i,i..' '..C.names[i],function() switch(name) end,{width=(geometry.width-64-16)/5,color=tab==name and C.brass or C.muted}))
 end
 local content=tab=='лимиты' and usageTab() or tab=='разбор' and triageTab() or tab=='бот' and botTab() or tab=='логи' and logsTab() or memoryTab()
 local freshness=age(tab=='лимиты' and (st.usage or {}).at or st.at)
 if busy or acting() then freshness='Выполняется действие · '..freshness end
 panel.render(C.frame(geometry,header,ui.row({key='tabs',height=30,gap=4},tabs),content,freshness,HELP[tab]))
 lastSignature=signature()
end
local LAYOUT={Cyrillic_er='h',Cyrillic_o='j',Cyrillic_el='k',Cyrillic_de='l',Cyrillic_pe='g',Cyrillic_te='n',
 Cyrillic_shcha='o',Cyrillic_ka='r',Cyrillic_yeru='s',Cyrillic_che='x',Cyrillic_en='y',Cyrillic_softsign='m',Cyrillic_ghe='u'}
function onKey(chord,pressed)
 if not pressed or not opened then return end
 local lower=string.lower(chord)
 local k=LAYOUT[chord] or (#chord==1 and lower) or chord
 if string.sub(chord,1,9)=='Cyrillic_' then k=LAYOUT['Cyrillic_'..string.lower(string.sub(chord,10))] or k end
 local number=tonumber(k)
 if number and TABS[number] then switch(TABS[number]); return end
 if k=='h' or k=='Left' then local at=table.find(TABS,tab) or 1; switch(TABS[(at-2)%#TABS+1]); return
 elseif k=='l' or k=='Right' then local at=table.find(TABS,tab) or 1; switch(TABS[at%#TABS+1]); return end
 if k=='j' or k=='Down' then sel[tab]+=1; clamp(); if expanded then expanded=nil;preview={} end; render(); return
 elseif k=='k' or k=='Up' then sel[tab]-=1; clamp(); if expanded then expanded=nil;preview={} end; render(); return
 elseif k=='g' then sel[tab]=sel[tab]==1 and #items() or 1; expanded=nil;preview={};clamp();render();return end
 if k=='Return' then local r=current(); if r then toggle(r) end; return end
 if k=='Escape' then panel.close(); return end
 if k=='r' then refresh();return elseif k=='o' then openFull();return end
 if not acting() and not busy and bot() then
  if k=='s' and not alive() and daemon()['есть'] then poke('bot-start');return
  elseif k=='x' and alive() then poke('bot-stop');return
  elseif k=='m' then poke('model-on');return elseif k=='u' then poke('model-off');return end
 end
 local r=current()
 if tab=='память' and (k=='y' or k=='n') then resolveMemory(r,k=='y' and 'promote' or 'reject')
 elseif tab=='разбор' and (k=='y' or k=='n') then flip(r,k=='y' and 'да' or 'нет') end
end
function onPanelGeometry(w,h,anchor,edge)
 geometry={width=math.max(1,w),height=math.max(1,h),anchor=anchor,edge=edge}
 render()
end
function onOpen()
 epoch+=1; opened=true;busy=false;confirm=nil;expanded=nil;preview={}
 errors.memory=''; st=noctalia.state.get(KEY) or {};tab='лимиты'
 panel.setWantsSecondTicks(true);render();poke('refresh');poke('usage')
end
function update()
 if opened and signature()~=lastSignature then render() end
end
function onClose()
 epoch+=1;opened=false;rows={};runs={};memoryRows={};live={};expanded=nil;preview={}
end
noctalia.state.watch(KEY,function(value) st=value or {};render();if opened and tab=='бот' then loadLive() end end)
