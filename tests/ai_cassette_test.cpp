#include "lua.h"
#include "scripting/luau_host.h"
#include "scripting/plugin_bindings.h"
#include "scripting/script_api_context.h"
#include "scripting/ui_prelude.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const std::filesystem::path directory(argv[1]);
  scripting::ScriptApiContext api;
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse(R"(
[plugins]
enabled=['q/skin-runtime','q/ai-triage']
[plugin_settings.'q/skin-runtime']
family='kumihimo'
applied_variant='11111111111111111111111111111111'
)")));
  scripting::PluginBindingContext context;
  scripting::ScriptSettings settings;
  LuauHost host(api, "q/ai-triage:triage");
  context.ownerId = host.runtimeName();
  context.host = &host;
  context.settings = &settings;
  host.setPluginDir(directory);
  host.setScriptContext(&context);
  scripting::registerPluginBindings(host.state(), &context);
  int failures = 0;
  // Test chunks mutate sandbox globals across calls. Disable Luau's load-time
  // import cache so a same-chunk assertion observes the action's updated value.
  lua_setsafeenv(host.state(), LUA_GLOBALSINDEX, false);
  const auto check = [&](bool condition, const char* why) {
    if (!condition) {
      ++failures;
      std::println(stderr, "ai_cassette: {} ({})", why, host.lastError());
    }
  };
  check(
      host.exec("=fixtures", R"(
state = {new=1, unsure=2, usage={at=1, error='offline', providers={
  {id='anthropic',title='Claude A',plan='Pro',error='',rows={{label='5h',fraction=0,text='0%',resetsAt=0}}},
  {id='vast',title='Vast.ai',error='no key',rows={}}
}}}
requests = {}; terminal = ''; closed = false; failMemory = false
triageRows = {{path='/fixture/card',title='Evidence',fresh=true,unread=true,verdict='?',share='warning',share_why='private',why='new'}}
memoryRows = {{id='s1',kind='session',path='/fixture/session',title='Session',preview='pending session'},
 {id='n1',kind='note',title='Note',preview='pending note',reviewer='owner',review_confidence=0.7}}
noctalia = setmetatable({
 nowMs=function() return 1000 end,
 readFile=function() return '---\ntitle: Evidence\n---\nActual evidence' end,
 state={get=function() return state end,watch=function(_,cb) watch=cb end},
 runInTerminal=function(cmd) terminal=cmd end,
 runAsync=function(cmd,cb)
  table.insert(requests,cmd)
  if not cb then return end
  local text = type(cmd)=='table' and table.concat(cmd,' ') or cmd
  local out=''
  if string.find(text,'triage.py rows',1,true) then out=noctalia.json.encode(triageRows)
  elseif string.find(text,'runs.py',1,true) then out=noctalia.json.encode({{['путь']='/fixture/log',['начало']='2026-10-12',['исход']='оборвалась',['хвост']='interrupted'}})
  elseif string.find(text,'memory_write.py rows',1,true) then out=noctalia.json.encode(memoryRows)
  elseif string.find(text,'tail -n',1,true) then out='Actual log\n⚠ warning' end
  cb({exitCode=(failMemory and string.find(text,'memory_write.py promote',1,true)) and 1 or 0,stdout=out,stderr='write denied'})
 end,
}, {__index=noctalia})
panel = setmetatable({close=function() closed=true end},{__index=panel})
function sent(part)
 for _,cmd in ipairs(requests) do
  local text=type(cmd)=='table' and table.concat(cmd,' ') or cmd
  if string.find(text,part,1,true) then return true end
 end
 return false
end
)"),
      "fixture setup failed"
  );
  check(host.exec("=ui-prelude", scripting::kUiPrelude), "UI prelude failed");
  std::ifstream file(directory / "panel.luau");
  const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  check(host.exec((directory / "panel.luau").string(), source), "panel load failed");
  const auto find = [&](const std::string& key) -> const ui::UiTreeNode* {
    if (!context.patch.uiTree)
      return nullptr;
    const auto visit = [&](const auto& self, const ui::UiTreeNode& node) -> const ui::UiTreeNode* {
      if (node.key == key)
        return &node;
      for (const auto& child : node.children)
        if (auto* result = self(self, child))
          return result;
      return nullptr;
    };
    return visit(visit, *context.patch.uiTree);
  };
  const auto click = [&](const std::string& key) {
    const auto* node = find(key);
    check(node != nullptr, "action disappeared");
    if (!node)
      return;
    const auto name = std::get<std::string>(node->props.at("onClick"));
    check(host.callGlobal(name.c_str()), "action handler failed");
  };
  check(host.exec("=open", "onPanelGeometry(900,732,137,'top'); onOpen()"), "opening failed");
  check(
      find("quota-1-1") && find("balance-2") && find("usage-error"),
      "zero quota, unknown balance or cached-data error was hidden"
  );
  check(
      find("cassette-mount") && std::get<double>(find("cassette-mount")->props.at("x")) == 95,
      "mount did not follow the edge-clamped opener"
  );
  check(host.exec("=triage", "onKey('2',true)"), "triage switch failed");
  check(find("entry-1") && find("evidence"), "triage list/evidence split missing");
  check(host.exec("=clear-requests", "requests={}"), "request reset failed");
  click("escalate");
  check(host.exec("=confirm-first", "assert(not sent(' all escalate'))"), "first paid click spent money");
  click("escalate");
  check(host.exec("=confirm-second", "assert(sent(' all escalate'))"), "second paid click did not execute");
  check(host.exec("=preview", "onKey('Return',true)"), "evidence opening failed");
  check(find("share-approved") && find("share-never"), "sharing permission controls missing");
  check(host.exec("=sharing-before", "assert(not sent(' allow '))"), "opening private evidence approved sharing");
  click("share-never");
  check(
      host.exec(
          "=sharing-denied",
          "assert(sent(\"allow '/fixture/card' never\")); assert(not sent(\"allow '/fixture/card' approved\"))"
      ),
      "explicit sharing denial became approval"
  );
  check(
      host.exec(
          "=bot",
          "onKey('3',true); onKey('o',true); assert(terminal=='journalctl --user -u chat-bot -f', "
          "'terminal='..tostring(terminal))"
      ),
      "bot journal keyboard access failed"
  );
  check(find("bot-unknown"), "missing bot status became a false stopped state");
  check(host.exec("=logs", "onKey('4',true); onKey('Return',true)"), "logs opening failed");
  check(find("log-tail") && find("entry-1"), "session/tail split missing");
  check(
      find("session-summary") && std::get<std::string>(find("session-summary")->props.at("text")) == "interrupted",
      "interrupted session lost its incident reason in the selected detail"
  );
  check(
      host.exec(
          "=memory-session",
          "onKey('5',true); requests={}; onKey('y',true); assert(terminal==\"nvim '/fixture/session'\", "
          "'terminal='..tostring(terminal)); assert(not sent(' promote '))"
      ),
      "session candidate was promoted instead of edited"
  );
  check(
      host.exec("=memory-failure", "failMemory=true; onKey('j',true); onKey('y',true)"),
      "memory failure scenario failed"
  );
  check(find("memory-error") && find("entry-2"), "failed memory write hid its error or candidate");
  check(
      host.exec(
          "=interlock",
          "state.bot={['демон']={['состояние']='active',['есть']=true},['модель']={['состояние']='выключена',['вкл']="
          "false}}; state.actingModel='loading'; watch(state); onKey('3',true); requests={}; onKey('u',true); "
          "onKey('x',true); assert(not sent(' all model-off') and not sent(' all bot-stop'))"
      ),
      "keyboard bypassed model/daemon interlock"
  );
  check(find("manual-off"), "intentional model-off state lost its explanation");
  check(
      find("model-state") && std::get<std::string>(find("model-state")->props.at("color")) == "#B5A38F",
      "intentional model-off was presented as a healthy running model"
  );
  check(
      host.exec("=keyboard-lists", "onKey('5',true); onKey('g',true); onKey('Cyrillic_O',true)"),
      "RU layout/list end navigation failed"
  );
  check(find("memory-error"), "memory action failure disappeared without a successful resolution");
  check(
      find("memory-title") && std::get<std::string>(find("memory-title")->props.at("text")) == "Note",
      "RU down key did not restore the selected note after jumping to the first entry"
  );
  check(
      host.exec("=unattached", "onPanelGeometry(720,580,-1,''); onKey('1',true)"), "narrow unattached fallback failed"
  );
  check(!find("cassette-mount") && find("usage-error"), "unattached fallback fabricated a bar mount or discarded data");
  check(
      host.exec("=widget-fixture", "state.actingModel=''; state.bot['модель']['состояние']='выключена'"),
      "manual-off widget fixture failed"
  );
  std::ifstream widgetFile(directory / "widget.luau");
  const std::string widgetSource{std::istreambuf_iterator<char>(widgetFile), std::istreambuf_iterator<char>()};
  check(host.exec((directory / "widget.luau").string(), widgetSource), "independent neural widget failed to load");
  check(
      find("neural-state") && std::get<std::string>(find("neural-state")->props.at("fill")) == "secondary",
      "intentional model-off became an alarm in the closed widget"
  );
  check(
      host.exec("=widget-failure", "state.bot['модель']['состояние']='спит'; watch(state)"),
      "model failure transition failed"
  );
  check(
      find("neural-state") && std::get<std::string>(find("neural-state")->props.at("fill")) == "error",
      "failed model retained the intentional-off indicator"
  );
  const auto activeConfig = api.configSnapshot();
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse(R"(
[plugins]
enabled=['q/skin-runtime','q/ai-triage']
[plugin_settings.'q/skin-runtime']
family='kumihimo'
)")));
  check(host.exec("=widget-inactive", "if onUpdate then onUpdate() end"), "inactive skin update failed");
  check(
      find("neural-entry-fallback") && !find("neural-mark"),
      "skin deactivation retained the wooden AI entry without a service-state change"
  );
  api.setConfigSnapshot(activeConfig);
  check(host.exec("=widget-reactivated", "if onUpdate then onUpdate() end"), "active skin update failed");
  check(
      find("neural-mark")
          && find("neural-state")
          && std::get<std::string>(find("neural-state")->props.at("fill")) == "error",
      "skin reactivation did not restore the neural mark and current failure state"
  );
  return failures == 0 ? 0 : 1;
}
