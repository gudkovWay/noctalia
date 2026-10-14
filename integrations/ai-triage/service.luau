--!nonstrict
-- Служба: единственный, кто считает состояние. Считает хозяйства бота,
-- разбора сессий Клода и AI-memory, потому что виджет в баре один.
--
-- Почему служба, а не сам виджет: экземпляр виджета живёт на каждом баре, то
-- есть на каждом мониторе, и опрос из него шёл бы втрое чаще без всякой
-- пользы. Плюс обход разбора идёт минутами — он не должен обрываться от того,
-- что панель закрыли.
--
-- Виджет и панель только читают `noctalia.state`; менять его отсюда — правило,
-- а не совпадение: два писателя в одно состояние разошлись бы при первой же
-- гонке между опросом и прогоном.
--
-- Ни одного числа здесь не считается: и `triage.py state`, и `status.py --json`
-- отвечают сами. Копия такой логики на Lua разъедется с оригиналом молча.
--
-- Команды приходят по IPC:
--   noctalia msg plugin q/ai-triage:watch all run|escalate|index
--   noctalia msg plugin q/ai-triage:watch all bot-start|bot-stop|bot-restart
--   noctalia msg plugin q/ai-triage:watch all model-on|model-off|model-toggle
--   noctalia msg plugin q/ai-triage:watch all refresh|usage|usage-invalidate|clear

local KEY = "ai_triage"
local AI = "/home/q/storage/ai"
local BOT = "/home/q/dev/chat-bot"
-- Vast отдаёт баланс только по ключу, а ключ лежит в файле: служба стартует из
-- сессии Hyprland, куда секреты из окружения не протекают.
local VAST_KEY = "/home/q/.secrets/vast/api_key"
local POLL_MS = 60000
local ompBin = noctalia.commandExists("omp") and "omp"
  or "/home/q/.cache/.bun/bin/omp" -- абсолютный путь нужен, когда PATH Noctalia урезан

-- OMP должен видеть НАСТОЯЩИЙ корень агента хозяина, а не HOME песочницы
-- Noctalia: там лежат и отчёты по провайдерам, и токен DeepSeek. Именованный
-- профиль (`OMP_PROFILE`/`PI_PROFILE`) перебивает каталог агента, поэтому его
-- снимаем, а каталог задаём явно. Домашний каталог, XDG и приватный bus при
-- этом остаются закрытыми: наружу уходит только этот путь, ни ключи, ни
-- хранилище целиком не читаются и не копируются. `env` вместо оболочки — чтобы
-- ни один символ команды не попал в чужой интерпретатор.
local OMP_AGENT_DIR = "/home/q/.omp/agent"
local function omp(argv)
  local out = { "env", "-u", "OMP_PROFILE", "-u", "PI_PROFILE",
    "PI_CODING_AGENT_DIR=" .. OMP_AGENT_DIR, ompBin }
  for _, arg in ipairs(argv) do
    table.insert(out, arg)
  end
  return out
end
local USAGE_MS = 300000       -- omp кэширует лимиты, поэтому пяти минут достаточно
local BALANCE_MS = 900000     -- баланс меняется медленнее лимитов, частый запрос избыточен
local VAST_MS = 300000        -- у Vast баланс тает на ходу, пяти минут хватает
local MAX_LINES = 200       -- строк лога держим ровно столько, сколько влезет
                            -- в панель прокруткой; остальное — в терминал

-- `systemctl stop` ждёт, пока демон уйдёт, а у него TimeoutStopSec=45: он
-- дописывает хвост сессии, гасит llama-server и контейнер морды. Таймаут
-- вызова обязан быть больше, иначе кнопка «Остановить» будет врать отказом
-- там, где всё идёт по плану.
local ACT_MS = 60000

-- Подъём модели: 10,7 с на загрузку, и это замер на здоровой машине с тёплым
-- дисковым кэшем. Холодный старт после перезагрузки дольше, а сам инструмент
-- ждёт до 120 с и только потом сдаётся. Таймаут вызова обязан быть больше
-- его собственного, иначе кнопка соврёт отказом там, где всё идёт по плану.
local MODEL_MS = 180000

local data = {
  new = 0, unsure = 0, warn = 0, yes = 0, no = 0, unread = 0, total = 0,
  running = false, lines = {}, tail = "", at = 0,
  memory_pending = 0, memory_disputed = 0, memory_promoted = 0,
  memory_rejected = 0, memory_total = 0, memory_review_running = false,
  bot = nil,
  acting = "",
  actingModel = "",
  usage = {
    at = 0, error = "", busy = false, worst = -1, providers = {},
  },
}

local usageAt = 0
local balanceAt = 0
local vastAt = 0
local vastBusy = false
local usageReports = {}
local deepseekBalance = { error = "ключ не отдался", text = "", currency = "" }
local vastBalance = { error = "", text = "" }

-- Сводку показываем один раз на прогон, а не на каждый опрос.
local announce = false

local function publish()
  data.at = os.time()
  noctalia.state.set(KEY, data)
end

-- В выводе triage.py есть цвета терминала: «⚠» приезжает обёрнутым в \27[31m.
-- В панели своя разметка, escape-последовательности там — мусор.
local function plain(line)
  return (string.gsub(line, "\27%[[0-9;]*m", ""))
end

-- Уведомляем через notify-send, а не через noctalia.notify. Разница не в
-- красоте: свои уведомления Noctalia помечает origin=internal и в
-- `~/.local/state/noctalia/notification_history.json` не кладёт вовсе —
-- всплыло, погасло, найти нечем. Через notify-send оно приходит как внешнее
-- и остаётся в истории. На том же месте уже обжигались с `urgency=low`,
-- который туда тоже не попадает.
local function quoted(s)
  return "'" .. string.gsub(s, "'", "'\\''") .. "'"
end

local function notify(urgency, timeoutMs, title, body)
  noctalia.runAsync(string.format("notify-send -a ai -u %s -t %d %s %s",
    urgency, timeoutMs, quoted(title), quoted(body)))
end

local function summary()
  return string.format("в индексе %d · отсеяно %d · спорных %d · небезопасных %d",
    data.yes, data.no, data.unsure, data.warn)
end

-- ────────────────────────────────────────────────────────── опрос

local function titleFor(id)
  return ({ anthropic = "Claude", ["openai-codex"] = "Codex", zai = "Z.AI",
    deepseek = "DeepSeek" })[id] or id
end

local function amountText(amount)
  if amount.unit == "percent" then
    local n = (tonumber(amount.usedFraction) or 0) * 100
    return n > 999 and ">999%" or string.format("%d%%", n)
  end
  if amount.used ~= nil and amount.limit ~= nil then
    return tostring(amount.used) .. "/" .. tostring(amount.limit)
  end
  return ""
end

local function buildProviders(reports)
  local grouped, reportOrder = {}, {}
  for _, report in ipairs(reports) do
    local id = report.provider or ""
    grouped[id] = grouped[id] or {}
    table.insert(grouped[id], report)
    if #grouped[id] == 1 then table.insert(reportOrder, id) end
  end
  local order = { "anthropic", "openai-codex", "zai" }
  for _, id in ipairs(reportOrder) do
    if id ~= "anthropic" and id ~= "openai-codex" and id ~= "zai" then table.insert(order, id) end
  end
  local providers, worst = {}, -1
  local seen = {}
  for _, id in ipairs(order) do
    if not seen[id] then
      seen[id] = true
      for _, report in ipairs(grouped[id] or {}) do
        local meta = report.metadata or {}
        local title = titleFor(id)
        if #grouped[id] > 1 then
          local mark = meta.email and string.match(meta.email, "^[^@]+")
            or string.sub(tostring(meta.accountId or ""), 1, 6)
          title = title .. " · " .. mark
        end
        local p = { id = id, title = title,
          plan = tostring(meta.planType or meta.tier or ""), error = "", rows = {} }
        for _, limit in ipairs(report.limits or {}) do
          local f = tonumber(limit.amount and limit.amount.usedFraction)
          if f and f >= 0 then
            table.insert(p.rows, { label = (limit.window or {}).label or limit.label or "",
              fraction = f, text = amountText(limit.amount), resetsAt = (limit.window or {}).resetsAt or 0 })
            if f > worst then worst = f end
          end
        end
        table.insert(providers, p)
      end
    end
  end
  if deepseekBalance.text ~= "" or deepseekBalance.error ~= "" then
    table.insert(providers, { id = "deepseek", title = "DeepSeek", plan = "",
      error = deepseekBalance.error or "", rows = deepseekBalance.text ~= "" and
        { { label = "Баланс", fraction = -1, text = deepseekBalance.text, resetsAt = 0 } } or {} })
  end
  -- Карточку Vast показываем всегда, ещё до первого ответа: место ей уже
  -- обещано, и появление строки на глазах — это мигание, а не данные.
  table.insert(providers, { id = "vast", title = "Vast.ai", plan = "",
    error = vastBalance.error or "", rows = vastBalance.text ~= "" and
      { { label = "Баланс", fraction = -1, text = vastBalance.text, resetsAt = 0 } } or {} })
  return providers, worst
end

local function pollUsage()
  data.usage.busy = true; publish()
  noctalia.runAsync(omp({ "usage", "--json" }), function(res)
    local got = res and res.exitCode == 0 and noctalia.json.decode(res.stdout or "")
    if type(got) ~= "table" or type(got.reports) ~= "table" then
      local why = res and noctalia.string.trim(res.stderr or "") or ""
      data.usage.error = why ~= "" and why or "omp usage не ответил"
    else
      usageReports = got.reports
      data.usage.providers, data.usage.worst = buildProviders(usageReports)
      data.usage.error, data.usage.at = "", os.time()
    end
    data.usage.busy = false; publish()
  end, 45000)
end
local function pollBalance()
  data.usage.busy = true; publish()
  -- Токен DeepSeek живёт только внутри этого вызова: он уходит в заголовок
  -- запроса к api.deepseek.com и ни в state, ни в лог, ни в отчёт не попадает.
  noctalia.runAsync(omp({ "token", "deepseek" }), function(res)
    local key = res and noctalia.string.trim(res.stdout or "")
    if not key or key == "" or res.exitCode ~= 0 then
      deepseekBalance = { error = "ключ не отдался", text = "" }
      data.usage.providers = buildProviders(usageReports)
      data.usage.busy = false; publish(); return
    end
    noctalia.http({ url = "https://api.deepseek.com/user/balance",
      headers = { "Authorization: Bearer " .. key } }, function(http)
      if not http or not http.ok then
        deepseekBalance = { error = "баланс не ответил: " .. tostring(http and http.status or 0), text = "" }
      else
        local got = noctalia.json.decode(http.body or "")
        local info = got and got.balance_infos and got.balance_infos[1]
        local value = info and info.total_balance
        deepseekBalance = value and { error = "", text = (info.currency == "USD" and "$" or info.currency == "CNY" and "¥" or "") .. tostring(value) ..
          ((info.currency ~= "USD" and info.currency ~= "CNY") and (" " .. tostring(info.currency or "XXX")) or "") } or { error = "баланс не ответил: " .. tostring(http.status), text = "" }
      end
      data.usage.providers, data.usage.worst = buildProviders(usageReports)
      data.usage.busy = false
      publish()
    end)
  end, 15000)
end

-- Из ответа публикуем только сумму, не профиль аккаунта или ключ.
-- Отдельный in-flight флаг не мешает опросу лимитов OMP.
local function pollVastBalance()
  if vastBusy then return end
  local key = noctalia.readFile(VAST_KEY)
  key = key and noctalia.string.trim(key) or ""
  if key == "" then
    vastBalance = { error = "ключ Vast не читается", text = "" }
    data.usage.providers, data.usage.worst = buildProviders(usageReports)
    publish()
    return
  end
  vastBusy = true
  noctalia.http({ url = "https://console.vast.ai/api/v0/users/current/",
    headers = { "Authorization: Bearer " .. key } }, function(http)
    if not http or not http.ok then
      vastBalance = { error = "баланс Vast недоступен: " .. tostring(http and http.status or 0), text = "" }
    else
      local ok, got = pcall(noctalia.json.decode, http.body or "")
      local value = ok and type(got) == "table" and got.credit or nil
      if type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge then
        vastBalance = { error = "", text = string.format("$%.2f", value) }
      else
        vastBalance = { error = "баланс Vast не разобран", text = "" }
      end
    end
    vastBusy = false
    data.usage.providers, data.usage.worst = buildProviders(usageReports)
    publish()
  end)
end

local function pollTriage()
  noctalia.runAsync(AI .. "/bin/triage.py state", function(res)
    if not res or res.exitCode ~= 0 then return end
    local got = noctalia.json.decode(res.stdout or "")
    if type(got) ~= "table" then return end
    for _, k in ipairs({ "new", "unsure", "warn", "yes", "no", "unread", "total" }) do data[k] = tonumber(got[k]) or 0 end
    data.running = got.running == true
    if announce and not data.running then announce = false; notify("normal", 8000, "Разбор закончен", summary()) end
    publish()
  end, 20000)
end

local function pollMemory()
  noctalia.runAsync({ "python3", AI .. "/bin/memory_write.py", "state" }, function(res)
    if not res or res.exitCode ~= 0 then return end
    local got = noctalia.json.decode(res.stdout or "")
    if type(got) ~= "table" then return end
    data.memory_pending = tonumber(got.pending) or 0
    data.memory_disputed = tonumber(got.disputed) or 0
    data.memory_promoted = tonumber(got.promoted) or 0
    data.memory_rejected = tonumber(got.rejected) or 0
    data.memory_total = tonumber(got.total) or 0
    data.memory_review_running = got.review_running == true
    publish()
  end, 20000)
end

local function startMemoryReview()
  if data.memory_review_running then return end
  data.memory_review_running = true
  publish()
  local ok = noctalia.runStream("python3 " .. AI .. "/bin/memory_review.py run", function(raw)
    local line = noctalia.string.trim(plain(raw))
    if line ~= "" then data.tail = line; publish() end
  end)
  if not ok then
    data.memory_review_running = false
    notify("critical", 0, "Память: обзор не запустился", "memory_review.py run")
  end
  pollMemory()
end

local function memoryEvent(event)
  if event == "memory-review" then
    startMemoryReview()
  elseif event == "memory-refresh" then
    pollMemory()
  end
end


local function pollBot()
  noctalia.runAsync(BOT .. "/tools/status.py --json", function(res)
    local got = res and res.exitCode == 0 and noctalia.json.decode(res.stdout or "")
    data.bot = type(got) == "table" and got or nil; publish()
  end, 20000)
end
local function poll()
  pollTriage(); pollBot(); pollMemory()
  local now = noctalia.nowMs()
  if now - usageAt >= USAGE_MS then usageAt = now; pollUsage() end
  if now - balanceAt >= BALANCE_MS then balanceAt = now; pollBalance() end
  if now - vastAt >= VAST_MS then vastAt = now; pollVastBalance() end
end



-- ────────────────────────────────────────────────────────── кнопки бота

-- Своей логики запуска здесь нет и не будет: три вызова systemd, юнит лежит в
-- репозитории бота (`deploy/chat-bot.service`) и залинкован. Всё, что знает
-- про то, как поднимать модель и морду, живёт в демоне.
-- Замок на обе кнопки сразу, а не по одной на каждую: пуск демона поднимает
-- модель, а «выгрузить модель» посреди этого подъёма — это гонка за один и
-- тот же llama-server. Кнопки в панели гаснут по тому же признаку.
local function busy()
  return data.acting ~= "" or data.actingModel ~= ""
end

local function service(verb, label)
  if busy() then
    return
  end
  data.acting = verb
  publish()
  noctalia.runAsync("systemctl --user " .. verb .. " chat-bot", function(res)
    data.acting = ""
    if not res or res.exitCode ~= 0 then
      local why = res and noctalia.string.trim(res.stderr or "") or "не запустилось"
      notify("critical", 0, "Бот: " .. label .. " не вышло", why)
    end
    poll()
  end, ACT_MS)
end


-- ────────────────────────────────────────────────────────── кнопки модели

-- Модель гасят и поднимают отдельно от демона: демон читает чат и ведёт лог
-- всегда, а семнадцать гигабайт нужны только тогда, когда бот должен
-- говорить. Автомат, ронявший модель под тяжёлой игрой, выключен 06.09.2026 —
-- решение оставлено человеку, и вот его кнопка.
--
-- Своей логики здесь снова нет: `tools/model.py` сам щёлкает выключателем,
-- сам поднимает и гасит сервер и сам же отвечает состоянием. Плагин зовёт его
-- и рисует ответ.
local function model(verb, label)
  if busy() then
    return
  end
  data.actingModel = verb
  publish()
  noctalia.runAsync(BOT .. "/tools/model.py " .. verb, function(res)
    data.actingModel = ""
    if not res or res.exitCode ~= 0 then
      local why = res and noctalia.string.trim(res.stderr or "") or "не запустилось"
      notify("critical", 0, "Модель: " .. label .. " не вышло", why)
    end
    poll()
  end, MODEL_MS)
end


-- ──────────────────────────────────────────────────────────── обход разбора

-- Поток строк: у runStream нет onClose, поэтому конец потока распознаётся по
-- последней строке самого скрипта. pass.sh обязан её напечатать в любом
-- исходе — и в нормальном, и через trap.
local function terminal(line)
  return string.match(line, "^готово")
      or string.match(line, "^прервано")
      or string.match(line, "^занято")
end

local function start(cmd, label)
  if data.running then
    return
  end
  data.running = true
  data.lines = { label }
  data.tail = label
  publish()

  local ok = noctalia.runStream(cmd, function(raw)
    local line = noctalia.string.trim(plain(raw))
    if line == "" then
      return
    end
    table.insert(data.lines, line)
    if #data.lines > MAX_LINES then
      table.remove(data.lines, 1)
    end
    data.tail = line
    if terminal(line) then
      data.running = false
      announce = not string.match(line, "^занято")
      pollTriage()                -- счётчики берём с диска, а не из строки
    end
    publish()
  end)

  if not ok then
    data.running = false
    data.tail = "не удалось запустить: " .. cmd
    notify("critical", 0, "Разбор не запустился", cmd)
    publish()
  end
end

function update()
  poll()
end

function onIpc(event, payload)
  if event == "run" then
    start(AI .. "/bin/pass.sh", "запускаю разбор…")
  elseif event == "escalate" then
    start(AI .. "/bin/pass.sh escalate", "спорные — Claude…")
  elseif event == "index" then
    start(AI .. "/bin/pass.sh index", "пересобираю индекс…")
  elseif event == "bot-start" then
    service("start", "пуск")
  elseif event == "bot-stop" then
    service("stop", "остановка")
  elseif event == "bot-restart" then
    service("restart", "перезапуск")
  elseif event == "model-on" then
    model("on", "подъём")
  elseif event == "model-off" then
    model("off", "выгрузка")
  elseif event == "model-toggle" then
    model("toggle", "переключение")
  elseif event == "usage" then
    usageAt = noctalia.nowMs(); pollUsage()
    vastAt = noctalia.nowMs(); pollVastBalance()
  elseif event == "usage-invalidate" then
    data.usage.busy = true; publish()
    vastAt = noctalia.nowMs(); pollVastBalance()
    noctalia.runAsync(omp({ "usage", "invalidate" }), function()
      usageAt = noctalia.nowMs(); pollUsage()
    end, 15000)
  elseif event == "refresh" then
    vastAt = noctalia.nowMs(); pollVastBalance()
    poll()
  elseif event == "memory-review" or event == "memory-refresh" then
    memoryEvent(event)
  elseif event == "clear" then
    data.lines = {}
    data.tail = ""
    publish()
  end
end

noctalia.setUpdateInterval(POLL_MS)
publish()
poll()
