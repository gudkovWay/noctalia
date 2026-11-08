--!nonstrict
-- Иконка в баре. Сама ничего не считает и не запускает — читает то, что
-- положила служба, и зовёт её по IPC. Экземпляров столько, сколько баров.
--
--   левая клавиша    открыть панель
--   средняя          запустить разбор, не открывая панель
--   правая           пересчитать состояние прямо сейчас
--
-- **Только глиф, без текста** — заказ хозяина 06.09.2026. Счётчик рядом с
-- роботом читался как «столько сообщений» и врал: это было число неразобранных
-- карточек Клода, к боту отношения не имеющее. Всё, что раньше стояло текстом,
-- теперь живёт в подсказке и в панели, а бар остаётся баром.
--
-- Подсказка начинается с остатков по моделям: на глиф наводят чаще всего
-- именно ради «сколько ещё можно жечь», а панель ради этого открывать долго.
--
-- Что осталось в самом глифе — цвет, и порядок тут важен:
--   идёт работа        loader, primary        разбор или нажатая кнопка
--   бот жив, модель:
--     на связи         robot,  primary        всё готово, можно писать в чат
--     поднимается      loader, secondary      ждать, это десять секунд
--     выключена        robot,  secondary      погашена вручную — так задумано
--     спит             robot,  error          её не поднял никто: это авария
--   требует решения    robot,  error          спорное и помеченное лежит
--   бот мёртв          robot,  on_surface_var тихо: он и должен быть выключен
--                                             под игрой, это не авария
--
-- Разница между «выключена» и «спит» — единственное, ради чего цвет вообще
-- смотрят: в первом случае молчание задумано, во втором бот молчит зря, и
-- узнать об этом хочется до того, как напишешь ему в чат. С 06.09.2026 модель
-- гасит только человек (`bot/switch.py`), автомат под игру выключен.

local KEY = "ai_triage"
local PANEL = "q/ai-triage:triage"
local SERVICE = "q/ai-triage:watch"
local SKIN = require("./desktop_skin.luau")

local function poke(event)
  noctalia.runAsync("noctalia msg plugin " .. SERVICE .. " all " .. event)
end

local function alive(s)
  local bot = s.bot
  return bot ~= nil and bot["демон"] ~= nil and bot["демон"]["состояние"] == "active"
end

local paintedKumihimo

local function paint(glyph, color)
  paintedKumihimo = SKIN.kumihimo() and not barWidget.isVertical()
  if paintedKumihimo then
    barWidget.render(ui.box({key="neural-entry",width=128,height=56,fill="#00000000"}, {
      ui.image({key="neural-timber",path="skin-assets/ai-cassette/slot.png",
        x=0,y=4,width=128,height=48,fit="stretch"}),
      ui.box({key="neural-slot-left",x=0,y=4,width=2,height=48,fill="#926749B0"}),
      ui.box({key="neural-slot-right",x=126,y=4,width=2,height=48,fill="#926749B0"}),
      ui.image({key="neural-mark",path="skin-assets/ai-cassette/neural.png",
        x=32,y=-4,width=64,height=64,fit="contain"}),
      ui.box({key="neural-state",x=40,y=49,width=48,height=1,fill=color}),
    }))
    return
  end
  -- Other skins use the same declarative renderer, without a retained timber slot.
  barWidget.render(ui.row({key="neural-entry-fallback",gap=0}, {
    ui.glyph({name=glyph,size=20,color=color}),
  }))
end

local function yesno(flag)
  return flag and "на связи" or "спит"
end

-- Слово про модель считает бот (`llm.state`), а не виджет: у него пять
-- состояний и у каждого своя починка, и вторая версия этого правила на Lua
-- разъехалась бы с первой молча.
local function modelWord(s)
  local b = s.bot
  return (b and b["модель"] and b["модель"]["состояние"]) or "?"
end

-- Лимиты — первыми строками подсказки, по строке на провайдера. Цвет глифа
-- они намеренно не трогают: он занят состоянием бота, и красный от «кончается
-- недельная квота» было бы не отличить от красного «модель спит», а чинят это
-- разным.
local function usageRows(u, rows)
  if type(u) ~= "table" then
    return
  end
  -- Служба не смогла спросить omp — это не «лимитов нет», и строка обязана
  -- говорить именно это.
  if (u.error or "") ~= "" then
    table.insert(rows, { key = "лимиты", value = u.error })
    return
  end
  for _, p in ipairs(u.providers or {}) do
    local parts = {}
    for _, r in ipairs(p.rows or {}) do
      table.insert(parts, tostring(r.label) .. " " .. tostring(r.text))
    end
    local value = "нет данных"
    if (p.error or "") ~= "" then
      value = p.error
    elseif #parts > 0 then
      value = table.concat(parts, " · ")
    end
    table.insert(rows, { key = string.lower(p.title or p.id or "?"), value = value })
  end
end

local function refresh(s)
  s = s or {}
  local decide = (tonumber(s.unsure) or 0) + (tonumber(s.warn) or 0)
  local memory = s.memory or {}
  local counts = memory.counts or {}
  local worker = (memory.worker or {}).status or "unknown"
  local memoryAttention = (counts.needs_owner or 0)+(counts.failed or 0)+(counts.blocked or 0)+(counts.policy_blocked or 0)>0
    or memory.provider_failure or (memory.error or "")~="" or worker=="failed" or worker=="disabled"
  local live = alive(s)

  if s.running or (s.acting or "") ~= "" or (s.actingModel or "") ~= "" then
    paint("loader", "primary")
  elseif memoryAttention then
    paint("robot", "error")
  elseif live then
    local word = modelWord(s)
    if word == "на связи" then
      paint("robot", "primary")
    elseif word == "спит" then
      paint("robot", "error")
    elseif word == "выключена" then
      paint("robot", "secondary")
    else
      paint("loader", "secondary")      -- поднимается или гаснет
    end
  elseif decide > 0 then
    paint("robot", "error")
  else
    paint("robot", "on_surface_variant")
  end

  local rows = {}
  usageRows(s.usage, rows)
  local bot = s.bot
  if bot == nil then
    -- Служба не смогла спросить status.py. Это не «бот мёртв», а «мы не знаем»,
    -- и подсказка обязана говорить именно это: чинить надо разное.
    table.insert(rows, { key = "бот", value = "не отвечает status.py" })
  else
    local d = bot["демон"] or {}
    -- Слова systemd — не для чтения: «inactive» в подсказке выглядит как
    -- недоделка, а `failed` от него на глаз не отличается вовсе. Незнакомое
    -- состояние отдаём как есть, врать про него нечем.
    local WORD = {
      active = "жив", inactive = "не запущен", failed = "упал",
      activating = "поднимается", deactivating = "останавливается",
    }
    local state = WORD[d["состояние"] or ""] or d["состояние"] or "?"
    if not d["есть"] then
      state = "юнит не залинкован"
    end
    table.insert(rows, { key = "бот", value = state })
    table.insert(rows, { key = "модель", value = modelWord(s) })
    table.insert(rows, { key = "морда", value = yesno((bot["морда"] or {})["жива"]) })
    local chat = bot["чат"] or {}
    table.insert(rows, { key = "сегодня в чате",
                         value = tostring(chat["сегодня"] or 0) })
    local run = bot["сессия"]
    if run and run["исход"] == "оборвалась" then
      table.insert(rows, { key = "прошлый запуск", value = "оборвался" })
    end
  end

  if s.running then
    table.insert(rows, { key = "идёт разбор", value = s.tail or "" })
  end
  table.insert(rows, { key = "архив: изменились карточки", value = tostring(tonumber(s.new) or 0) })
  if (tonumber(s.unsure) or 0) > 0 then
    table.insert(rows, { key = "архив: допуск не решён", value = tostring(s.unsure) })
  end
  local workerNames = {running="worker работает",waiting="таймер ожидает",disabled="таймер выключен",failed="сбой worker",unknown="состояние неизвестно"}
  table.insert(rows, {key="память v2",value=(memory.error or "")~="" and memory.error or workerNames[worker] or worker})
  if memory.counts then
    table.insert(rows, {key="память: очередь",value=tostring(counts.pending+counts.running+counts.applied).." фрагм. / "..tostring(memory.queue_sources or 0).." сессий"})
    table.insert(rows, {key="память: доставлено",value=tostring(counts.indexed).." пакетов"})
    table.insert(rows, {key="память: вопросы / без доказательств",value=tostring(counts.needs_owner).." / "..tostring(counts.needs_evidence)})
    table.insert(rows, {key="память: блокировки",value=tostring(counts.failed+counts.blocked+counts.policy_blocked)})
    table.insert(rows, {key="память: снимок",value=tostring(math.max(0,math.floor((os.time()-(memory.at or 0))/60))).." мин назад"})
  end
  if memory.provider_failure then table.insert(rows,{key="reviewer",value="остановлен после ошибки, без автоповтора"}) end
  if (tonumber(s.warn) or 0) > 0 then
    table.insert(rows, { key = "небезопасных", value = tostring(s.warn) })
  end
  barWidget.setTooltip(rows)
end

function onUpdate()
  if (SKIN.kumihimo() and not barWidget.isVertical()) ~= paintedKumihimo then
    refresh(noctalia.state.get(KEY))
  end
end

function onClick()
  noctalia.togglePanel(PANEL)
end

function onRightClick()
  poke("refresh")
end

noctalia.state.watch(KEY, refresh)
refresh(noctalia.state.get(KEY))
