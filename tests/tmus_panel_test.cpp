#include "lua.h"
#include "render/core/image_file_loader.h"
#include "scripting/luau_host.h"
#include "scripting/plugin_bindings.h"
#include "scripting/script_api_context.h"
#include "scripting/ui_prelude.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <string>
#include <vector>

namespace {
std::string read(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) {
  if (argc != 3) return 1;
  const std::filesystem::path directory(argv[1]);
  scripting::ScriptApiContext api;
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse(R"(
[plugins]
enabled=['q/skin-runtime','q/tmus']
[bar.mina-left]
enabled=true
)")));
  scripting::ScriptSettings settings;
  scripting::PluginBindingContext context;
  LuauHost host(api, "q/tmus:library");
  context.ownerId=host.runtimeName();context.host=&host;context.settings=&settings;
  host.setPluginDir(directory);host.setScriptContext(&context);
  scripting::registerPluginBindings(host.state(),&context);
  lua_setsafeenv(host.state(),LUA_GLOBALSINDEX,false);
  int failures=0;
  const auto check=[&](bool value,const char* why) {
    if (!value) {++failures;std::println(stderr,"tmus_panel: {} ({})",why,host.lastError());}
  };
  const auto exec=[&](const char* source) {return host.exec("=music-behavior",source);};
  check(host.exec("=fixture",read(argv[2])),"fixture failed");
  check(host.exec("=ui",scripting::kUiPrelude),"native UI prelude failed");
  check(exec("panel.close=function() testClosed=true end"),"close boundary failed");
  check(host.exec((directory/"panel.luau").string(),read(directory/"panel.luau")),"production panel failed to load");
  const auto find=[&](const std::string& key)->const ui::UiTreeNode* {
    if (!context.patch.uiTree) return nullptr;
    const auto visit=[&](const auto& self,const ui::UiTreeNode& node)->const ui::UiTreeNode* {
      if (node.key==key) return &node;
      for (const auto& child:node.children) if (auto* found=self(self,child)) return found;
      return nullptr;
    };
    return visit(visit,*context.patch.uiTree);
  };
  const auto textIs=[&](const std::string& key,const std::string& value) {
    const auto* node=find(key);
    return node && node->props.contains("text") && std::get<std::string>(node->props.at("text"))==value;
  };
  const auto invoke=[&](const std::string& key,const char* event,std::vector<scripting::ScriptArg> args={}) {
    const auto* node=find(key);
    check(node && node->props.contains(event),"reachable action missing");
    if (!node || !node->props.contains(event)) return;
    const auto name=std::get<std::string>(node->props.at(event));
    check(host.callGlobalWithArgsAndBudget(name.c_str(),args,std::chrono::milliseconds(100)),"action failed");
  };
  const auto click=[&](const std::string& key) {invoke(key,"onClick");};
  check(exec("onPanelGeometry(1180,760,-1,'left');onOpen()"),"open failed");
  check(find("mina-shelf-1") && !find("mina-home-state"),
        "opening after the service published recommendations ignored the cached feed");
  check(find("music-cape") && find("mina-tab-home") && find("mina-tab-eq"),"Mina cloak or five destinations missing");
  check(find("mina-shelf-1") && find("mina-shelf-2"),
        "recommendations left space for a second shelf but rendered only one");
  check(exec("onPanelGeometry(1180,756,-1,'left')"),"exact two-shelf height failed");
  check(find("mina-shelf-2"),"second shelf disappeared at the exact available-height boundary");
  check(exec("onPanelGeometry(1180,755,-1,'left')"),"short shelf viewport failed");
  check(!find("mina-shelf-2"),"two shelves overflow a viewport that is one pixel too short");
  check(exec(R"(
onPanelGeometry(1180,760,-1,'left')
for i=3,4 do
 local shelf=table.clone(testHome.shelves[2]);shelf.title='Shelf '..i
 table.insert(testHome.shelves,shelf)
end
testWatches['tmus.home'](testHome);testCalls={}
)"),"recommendation navigation fixture failed");
  click("mina-home-2-1");
  check(exec("onKey('J',true);onKey('J',false)"),"next recommendation shelf failed");
  check(find("mina-shelf-2") && find("mina-shelf-3") && !find("mina-shelf-1"),
        "moving below the visible recommendation window lost a shelf or left the cursor hidden");
  check(exec("assert(#testCalls==0);onPanelGeometry(900,680,-1,'left')"),
        "browsing recommendations started playback or narrow geometry failed");
  check(find("mina-shelf-3") && !find("mina-shelf-2") && !find("mina-shelf-4"),
        "short recommendation viewport retained an overflowing two-shelf window");
  check(exec("onPanelGeometry(1180,760,-1,'left')"),"recommendation viewport recovery failed");
  click("mina-tab-home");
  check(exec("testHome.status='loading';testWatches['tmus.home'](testHome)"),"home loading transition failed");
  check(textIs("mina-home-state","Загрузка рекомендаций…"),"loading recommendations became an empty catalogue");
  check(exec("testHome.status='ok';testWatches['tmus.home'](testHome)"),"home recovery failed");
  check(exec("onClose();testHome={status='error',error='Provider unavailable',shelves={}};onOpen()"),
        "closed-panel recommendation update failed");
  check(textIs("mina-home-state","Provider unavailable"),
        "reopening retained a stale feed instead of reading the current service state");
  check(exec("testHome={status='ok',shelves={}};testWatches['tmus.home'](testHome)"),
        "empty recommendation update failed");
  check(textIs("mina-home-state","Рекомендаций пока нет"),"empty recommendations lack a terminal state");
  click("mina-tab-library");
  click("mina-playlist-1");
  check(find("mina-expanded-1") && find("mina-track-6") && !find("np-full"),"expanded selection, six-row budget or transport ownership failed");
  check(exec("testCalls={}"),"request reset failed");
  click("mina-track-4");
  check(find("mina-expanded-4") && !find("mina-expanded-1"),"selection did not move expansion");
  check(exec("assert(#testCalls==0)"),"selecting a row unexpectedly started playback");
  click("mina-track-play-4");
  check(exec("assert(testSent('play-playlist','local:night|soundcloud:track-4'))"),"explicit play lost exact playlist context");
  click("mina-track-like-4");
  check(exec("assert(testSent('rate','like:soundcloud:track-4'));assert(not testSent('rate','like:ytmusic:night'))"),"row action targeted playing track instead of its named row");
  check(exec("testCalls={};onKey('Tab',true);onKey('Tab',false)"),"queue focus toggle failed");
  click("mina-queue-1");
  check(exec("assert(#testCalls==0);onKey('Return',true);onKey('Return',false);assert(testSent('queue-goto','2'))"),"queue selection played or shuffled display index was sent as canonical index");
  click("music-volume-control");
  check(exec("testCalls={};onKey('L',true);onKey('L',false);assert(testSent('vol-up'));onKey('Escape',true)"),"music gain keyboard mode failed");
  check(exec("assert(not testClosed)"),"volume Escape closed the panel");
  check(exec("testCalls={}"),"gain request reset failed");
  invoke("music-gain","onChange",{std::string("55")});
  check(exec("assert(#testCalls==0)"),"gain drag preview committed early");
  invoke("music-gain","onDragEnd",{std::string("55")});
  check(exec("assert(#testCalls==1 and testSent('vol-set','55'))"),"gain did not commit exactly once to tmusd");
  click("mina-tab-search");
  const auto* searchPane=find("mina-main-pane");
  const auto* searchList=find("mina-track-list");
  check(searchPane && searchList,"search viewport or results missing");
  if (searchPane && searchList) {
    double available=std::get<double>(searchPane->props.at("height"))
        - std::get<double>(searchPane->props.at("gap"))*static_cast<double>(searchPane->children.size()-1);
    for (const auto& child:searchPane->children) {
      if (child.type=="scroll") continue;
      const char* extent=child.type=="separator"?"thickness":"height";
      available-=std::get<double>(child.props.at(extent));
    }
    double rowsHeight=0;
    for (const auto& row:searchList->children) rowsHeight+=std::get<double>(row.props.at("height"));
    check(rowsHeight<=available,"search window clips the last rendered result below its controls");
  }
  check(exec("testCalls={};onKey('X',true);onKey('X',false);onKey('Return',true);onKey('Return',true);assert(testSent('search'));assert(not testSent('play-context'));onKey('Return',false);onKey('Return',true);onKey('Return',false);assert(testSent('play-context'))"),"search submit press became playback or fresh Enter broke");
  click("mina-tab-clients");
  check(textIs("mina-client-state-2","Готов") && textIs("mina-client-state-3","Без входа") && textIs("mina-client-state-4","Сессия истекла"),"provider authentication states were conflated");
  click("mina-service-menu");
  click("mina-menu-restart");
  check(find("mina-confirm-cancel") && find("mina-confirm-apply"),"restart lacks destructive confirmation");
  check(exec("testCalls={};onKey('Return',true);onKey('Return',false);assert(not testSent('restart-daemon'))"),"default confirmation was not Cancel");
  click("mina-tab-eq");
  check(find("mina-eq-band-10") && find("mina-eq-band-1"),"ten-band EQ disappeared");
  invoke("mina-eq-band-1","onChange",{std::string("-4")});
  invoke("mina-eq-band-1","onDragEnd",{std::string("-4")});
  check(exec("assert(testSent('eq','band:1:-4'))"),"bipolar EQ control did not reach backend");
  click("mina-eq-flat");
  check(exec("assert(testSent('eq','flat'));assert(not testSent('eq','on'))"),"Flat implicitly enabled EQ");
  check(exec("onPanelGeometry(900,680,-1,'left');onKey('2',true);onKey('Tab',true);onKey('Tab',false)"),"narrow buffer toggle failed");
  check(find("mina-queue-pane") && !find("mina-main-pane") && find("mina-tab-eq"),"narrow queue became a sixth destination or covered main content");
  check(exec("onKey('Tab',true);onKey('Tab',false)"),"narrow return failed");
  click("mina-tab-library");
  click("mina-playlist-1");
  click("mina-view-action");
  click("mina-menu-remove");
  check(find("mina-confirm-cancel"),"playlist removal bypassed confirmation");
  check(exec("onClose();onOpen()"),"close lifecycle failed");
  check(!find("mina-confirm-apply") && !find("mina-menu"),"reopen retained a stale destructive target");
  const auto cape=loadImageFile((directory/"skin-assets/mina/cape.png").string());
  check(cape.has_value(),"native image decoder rejected music cloak");
  if (cape) {
    check(cape->width==2360 && cape->height==1520,"music cloak density changed");
    bool covered=true;
    for (int y=146*2;y<610*2;y+=16)
      for (int x=56*2;x<1014*2;x+=16)
        covered=covered && cape->rgba[(static_cast<std::size_t>(y)*cape->width+x)*4+3]>=240;
    check(covered,"music content crosses transparent cape silhouette");
  }
  std::println("tmus_panel: {} failures",failures);
  return failures==0?0:1;
}
