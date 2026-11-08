#include "lua.h"
#include "render/core/image_file_loader.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "scripting/luau_host.h"
#include "scripting/plugin_bindings.h"
#include "scripting/script_api_context.h"
#include "scripting/ui_prelude.h"
#include "ui/controls/flex.h"
#include "ui/controls/scroll_view.h"
#include "ui/ui_tree_reconciler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>

std::unique_ptr<TextureManager> createDefaultTextureManager() { return nullptr; }

namespace {
class LayoutRenderer : public Renderer {
public:
  TextMetrics measureText(std::string_view text, float size, FontWeight, float, int, TextAlign,
                          std::string_view, TextEllipsize, bool) override {
    return TextMetrics{.width=static_cast<float>(text.size())*size*0.5F,.bottom=size};
  }
  TextMetrics measureFont(float size, FontWeight) override { return TextMetrics{.bottom=size}; }
  void measureTextCursorStops(std::string_view, float, const std::vector<std::size_t>&,
                             std::vector<float>&, FontWeight) override {}
  void measureTextCursorStopsWrapped(std::string_view, float size,
      const std::vector<std::size_t>& offsets, float, std::vector<TextCursorStop>& stops,
      FontWeight) override {
    stops.assign(offsets.size(),TextCursorStop{0.0F,0.0F,size});
  }
  TextMetrics measureGlyph(char32_t, float size) override {
    return TextMetrics{.width=size,.bottom=size};
  }
  TextureManager& textureManager() override { std::abort(); }
  [[nodiscard]] float renderScale() const noexcept override { return 1.0F; }
};
}

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
  lua_setsafeenv(host.state(), LUA_GLOBALSINDEX, false);
  int failures = 0;
  const auto check = [&](bool condition, const char* why) {
    if (!condition) {
      ++failures;
      std::println(stderr, "ai_cassette: {} ({})", why, host.lastError());
    }
  };
  check(host.exec("=fixtures", R"(
state = {usage={at=1,error='offline',providers={
 {id='anthropic',title='Claude A',plan='Pro',error='',rows={{label='5h',fraction=0,text='0%',resetsAt=0}}},
 {id='openai',title='Codex',error='quota unavailable',rows={{label='5h',fraction=-1,text='unknown'}}},
 {id='vast',title='Vast.ai',error='no key',rows={}}
}},memory={at=1,worker={status='waiting'},counts={pending=1,running=0,applied=0,indexed=1,no_change=0,needs_owner=1,failed=0,blocked=0,policy_blocked=0,needs_evidence=0}}}
requests={};terminal='';closed=false;failMemory=false;deferMemory=false;pendingMemory=nil
triageRows={{path='/fixture/card',title='Evidence',fresh=true,unread=true,verdict='?',share='warning',share_why='private',why='new'}}
memoryRows={
 {id='done-1',state='indexed',title='Applied package',source='session',revision='r1',applied_count=1,question_count=0,reason='indexed'},
 {id='question-1',state='needs_owner',title='Owner question',source='session',revision='r2',applied_count=0,question_count=1,reason='conflicting decisions'}
}
noctalia=setmetatable({
 nowMs=function() return 1000 end,
 readFile=function() return '---\ntitle: Evidence\n---\nActual evidence' end,
 state={get=function() return state end,watch=function(_,cb) watch=cb end},
 runInTerminal=function(cmd) terminal=cmd end,
 runAsync=function(cmd,cb)
  table.insert(requests,cmd)
  if not cb then return end
  local text=type(cmd)=='table' and table.concat(cmd,' ') or cmd
  local out=''
  if string.find(text,'triage.py rows',1,true) then out=noctalia.json.encode(triageRows)
  elseif string.find(text,'runs.py',1,true) then out=noctalia.json.encode({{['путь']='/fixture/log',['начало']='2026-10-12',['исход']='оборвалась',['хвост']='interrupted'}})
  elseif string.find(text,'memory_cli.py delivery ',1,true) then
   if deferMemory then pendingMemory=cb;return end
   if failMemory then cb({exitCode=1,stdout='',stderr='delivery offline'});return end
   out=noctalia.json.encode({version=2,rows=memoryRows,total=7,at=1})
  elseif string.find(text,'memory_cli.py delivery-job',1,true) then out='Receipt evidence'
  elseif string.find(text,'tail -n',1,true) then out='Actual log\nwarning' end
  cb({exitCode=0,stdout=out,stderr=''})
 end,
},{__index=noctalia})
panel=setmetatable({close=function() closed=true end},{__index=panel})
function sent(part)
 for _,cmd in ipairs(requests) do
  local text=type(cmd)=='table' and table.concat(cmd,' ') or cmd
  if string.find(text,part,1,true) then return true end
 end
 return false
end
)"), "fixture setup failed");
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
  const auto textIs = [&](const std::string& key, const std::string& expected) {
    const auto* node = find(key);
    return node && node->props.contains("text") && std::get<std::string>(node->props.at("text")) == expected;
  };
  const auto enabled = [&](const std::string& key) {
    const auto* node = find(key);
    return node && (!node->props.contains("enabled") || std::get<bool>(node->props.at("enabled")));
  };
  const auto click = [&](const std::string& key) {
    const auto* node = find(key);
    check(node && node->props.contains("onClick"), "reachable pointer action disappeared");
    if (!node || !node->props.contains("onClick"))
      return;
    const auto name = std::get<std::string>(node->props.at("onClick"));
    check(host.callGlobal(name.c_str()), "action handler failed");
  };
  check(host.exec("=open", "onPanelGeometry(900,732,137,'top');onOpen()"), "opening failed");
  check(find("quota-1-1") && find("quota-2-1") && find("balance-3") && find("usage-error"),
        "zero/unknown subscription quota, missing balance or cached-data error was hidden/misclassified");
  check(textIs("title", "Лимиты"), "current area is not the sole header title");
  check(find("cape-material") && !find("interior-surface") && !find("cassette-mount"),
        "AI panel retained a rectangular backplate or legacy mount instead of the tray cape");
  click("tab-selector");
  check(find("section-menu") && find("section-1") && find("section-5"), "title did not expose all existing areas");
  click("section-2");
  check(textIs("title", "Архив") && !find("section-menu") && find("evidence"),
        "pointer tab selection failed to load archive or dismiss the menu");
  check(host.exec("=archive-no-side-effects", "assert(not sent('escalate') and not sent('promote'))"),
        "opening archive caused a paid review or memory promotion");
  check(host.exec("=preview", "onKey('Return',true)"), "evidence opening failed");
  check(find("share-approved") && find("share-never"), "explicit sharing controls missing");
  check(host.exec("=sharing-before", "assert(not sent(' allow '))"), "opening evidence approved sharing");
  click("share-never");
  check(host.exec("=sharing-denied", "assert(sent(\"allow '/fixture/card' never\"));assert(not sent(\"allow '/fixture/card' approved\"))"),
        "explicit sharing denial became approval");
  check(host.exec("=bot", "onKey('3',true);onKey('o',true);assert(terminal=='journalctl --user -u chat-bot -f')"),
        "bot journal keyboard access failed");
  check(find("bot-unknown"), "missing bot status became a false stopped state");
  check(host.exec("=logs", "onKey('4',true);onKey('Return',true)"), "logs opening failed");
  check(find("log-tail") && textIs("session-summary", "interrupted"), "session tail or incident reason missing");
  check(host.exec("=memory", "onKey('5',true)"), "memory delivery opening failed");
  check(find("memory-detail") && textIs("memory-title", "Applied package"), "delivery v2 package not shown");
  check(!enabled("memory-discuss") && !enabled("memory-prev") && enabled("memory-next"),
        "indexed package gained an owner-question action or pagination bounds failed");
  check(host.exec("=question", "onKey('j',true)"), "memory selection failed");
  check(textIs("memory-title", "Owner question") && enabled("memory-discuss"), "unresolved question lost its exact action");
  click("memory-discuss");
  check(host.exec("=question-action", "assert(string.find(terminal,'delivery-job question-1',1,true));assert(not sent('promote') and not sent('reviewer'))"),
        "question action became a memory approval or reviewer retry");
  check(host.exec("=memory-error", "failMemory=true;onKey('r',true)"), "memory error transition failed");
  check(find("memory-load-error") && textIs("memory-title", "Owner question") && !enabled("memory-discuss"),
        "delivery failure lost cached records or retained owner action on stale data");
  check(host.exec("=memory-loading", "failMemory=false;deferMemory=true;onKey('r',true)"), "memory loading transition failed");
  check(!enabled("memory-discuss") && !enabled("memory-next"), "loading permitted a conflicting question or page action");
  check(host.exec("=memory-result", "deferMemory=false;pendingMemory({exitCode=0,stdout=noctalia.json.encode({version=2,rows=memoryRows,total=7,at=2})});pendingMemory=nil"),
        "memory response failed");
  check(enabled("memory-discuss"), "fresh successful delivery did not restore the owner-question action");
  check(host.exec("=interlock", R"(
state.bot={['демон']={['состояние']='active',['есть']=true},['модель']={['состояние']='выключена',['вкл']=false}}
state.actingModel='loading';watch(state);onKey('3',true);requests={};onKey('u',true);onKey('x',true)
assert(not sent(' all model-off') and not sent(' all bot-stop'))
)"), "keyboard bypassed model/daemon interlock");
  check(find("manual-off") && !enabled("bot-stop"), "intentional model-off explanation or busy interlock missing");
  check(host.exec("=cycle", "onKey('5',true);onKey('Right',true)"), "cyclic keyboard navigation failed");
  check(textIs("title", "Лимиты"), "last-to-first tab wrap failed");
  check(host.exec("=ru-cycle", "onKey('Cyrillic_er',true)"), "RU previous tab navigation failed");
  check(textIs("title", "Память"), "RU previous-tab physical key changed its destination");
  click("tab-selector");
  check(host.exec("=menu-keyboard", "onKey('4',true)"), "keyboard tab selection failed while title menu was open");
  check(textIs("title", "Логи") && !find("section-menu"), "keyboard selection left obsolete title menu covering the content");
  check(host.exec("=unattached", "onPanelGeometry(720,580,-1,'');onKey('1',true)"), "narrow unattached layout failed");
  check(!find("cassette-mount") && find("usage-error") && find("content-viewport"),
        "unattached fallback fabricated a mount or discarded cached data");
  check(host.exec("=close-menu", "closed=false"), "close fixture reset failed");
  click("tab-selector");
  check(host.exec("=escape", "onKey('Escape',true);assert(closed)"), "Escape stopped closing the panel immediately");
  check(host.exec("=reopen", "onClose();onOpen()"), "panel lifecycle failed");
  check(textIs("title", "Лимиты") && !find("section-menu"), "reopened panel retained stale navigation overlay");
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse("[plugins]\nenabled=[]\n")));
  LayoutRenderer renderer;
  const auto material = loadImageFile((directory / "skin-assets/ai-cassette/cape.png").string());
  check(material.has_value(), "native loader cannot decode the cape material");
  for (const auto dimensions : {std::pair{900,671},std::pair{720,580}}) {
    const auto [width,height]=dimensions;
    check(host.exec("=layout-size","onPanelGeometry("+std::to_string(width)+","+
      std::to_string(height)+",-1,'');onKey('1',true)"),"layout input failed");
    Flex scene;
    scene.setSize(static_cast<float>(width),static_cast<float>(height));
    ui::UiTreeReconciler reconciler;
    reconciler.setPathResolver([](const std::string&) { return std::string{}; });
    reconciler.reconcile(scene,*context.patch.uiTree,renderer);
    scene.layout(renderer);
    float viewportWidth=0;
    float viewportHeight=0;
    float viewportX=0;
    float viewportY=0;
    const auto visit=[&](const auto& self,const Node& node)->void {
      if (const auto* scroll=dynamic_cast<const ScrollView*>(&node)) {
        if (scroll->width()>viewportWidth) {
          viewportWidth=scroll->width();
          viewportHeight=scroll->height();
          Node::absolutePosition(scroll,viewportX,viewportY);
        }
      }
      for (const auto& child:node.children())
        self(self,*child);
    };
    visit(visit,scene);
    check(std::abs(viewportWidth-static_cast<float>(width-120))<1,
      "native viewport no longer uses the full cape content width");
    check(std::abs(viewportHeight-static_cast<float>(height-236))<1,
      "native viewport collapsed above the reserved cape hem");
    if (material) {
      bool reported=false;
      const auto opaque = [&](float x, float y) {
        const int px=std::clamp(static_cast<int>(x*material->width/width),0,material->width-1);
        const int py=std::clamp(static_cast<int>(y*material->height/height),0,material->height-1);
        const bool solid=material->rgba[(py*material->width+px)*4+3]>=240;
        if (!solid && !reported) {
          reported=true;
          std::println(stderr,"ai_cassette: transparent content at {},{} in {}x{}",x,y,width,height);
        }
        return solid;
      };
      bool covered=true;
      for (float y=viewportY;y<viewportY+viewportHeight;y+=8)
        for (float x=viewportX;x<viewportX+viewportWidth;x+=8)
          covered=covered && opaque(x,y);
      for (const auto* key : {"cassette-content", "freshness"}) {
        if (const auto* node=find(key)) {
          const float x=static_cast<float>(std::get<double>(node->props.at("x")));
          const float y=static_cast<float>(std::get<double>(node->props.at("y")));
          const float w=static_cast<float>(std::get<double>(node->props.at("width")));
          const float bottom=y+(std::string_view(key)=="freshness" ? 20.0F : 44.0F);
          for (float offset=0;offset<w;offset+=8)
            covered=covered && opaque(x+offset,y) && opaque(x+offset,bottom);
        }
      }
      check(covered, "panel text/actions extend beyond the painted cloth into a transparent cutout");
      check(material->rgba[3]==0, "cape exterior became an opaque rectangular backing");
    }
  }
  return failures == 0 ? 0 : 1;
}
