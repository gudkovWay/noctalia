#include "lua.h"
#include "scripting/luau_host.h"
#include "scripting/plugin_bindings.h"
#include "scripting/script_api_context.h"
#include "scripting/ui_prelude.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const std::filesystem::path directory(argv[1]);
  scripting::ScriptApiContext api;
  int failures = 0;
  const auto run = [&](LuauHost& host, const std::string& name, const std::string& source) {
    if (!host.exec(name, source)) {
      ++failures;
      std::println(stderr, "tmus_integration: {} ({})", name, host.lastError());
    }
  };
  const auto load = [&](LuauHost& host, const char* name) {
    std::ifstream file(directory / name);
    run(host, (directory / name).string(), {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()});
  };
  const auto bind = [&](LuauHost& host, scripting::PluginBindingContext& context, scripting::ScriptSettings& settings) {
    context.ownerId = host.runtimeName();
    context.host = &host;
    context.settings = &settings;
    host.setPluginDir(directory);
    host.setScriptContext(&context);
    scripting::registerPluginBindings(host.state(), &context);
    lua_setsafeenv(host.state(), LUA_GLOBALSINDEX, false);
  };
  {
    scripting::PluginBindingContext context;
    scripting::ScriptSettings settings;
    LuauHost host(api, "q/tmus:hint-test");
    bind(host, context, settings);
    run(host, "=hint-navigation", R"(
local Hint=require('./hint_navigation.luau')
assert(Hint.letter('Cyrillic_ef')=='a' and Hint.letter('Cyrillic_de')=='l')
local h=Hint.new()
local selected={}
local function tree(identity)
 return {type='row',props={key='root'},children={
  {type='button',props={key='play',text='Play',hintIdentity=identity,onClick=function() table.insert(selected,'play') end}},
  {type='button',props={key='next',text='Next',onClick=function() table.insert(selected,'next') end}},
  {type='button',props={key='hidden',text='Hidden',visible=false,onClick=function() error('hidden action') end}},
  {type='button',props={key='disabled',text='Disabled',enabled=false,onClick=function() error('disabled action') end}},
 }}
end
h:render(tree('track-a'),'primary')
assert(#h.targets==2 and h:start('F'))
h:press('A');h:press('A')
assert(#selected==0)
h:release('A');h:press('A')
assert(#selected==1 and selected[1]=='play' and not h:isActive())
h:release('F');h:release('A')
assert(h:start('F'))
h:render(tree('track-a'),'primary')
assert(h:isActive(), 'redraw closures invalidated unchanged targets')
h:render(tree('track-b'),'primary')
assert(h:isStale() and not h:isActive() and #selected==1)
h:reset();h:render(tree('track-b'),'primary');assert(h:start('F'))
h:press('Cyrillic_ef');h:release('Cyrillic_ef');h:press('Cyrillic_ef')
assert(#selected==2 and selected[2]=='play')
)");
  }
  {
    scripting::PluginBindingContext context;
    scripting::ScriptSettings settings;
    LuauHost host(api, "q/tmus:watch");
    bind(host, context, settings);
    run(host, "=service-fixtures", R"(
pending={};published={}
noctalia=setmetatable({
 commandExists=function() return true end,
 nowMs=function() return 1000 end,
 setUpdateInterval=function() end,
 pluginDataDir=function() return '' end,
 fileExists=function() return false end,
 runStream=function() return false end,
 state={set=function(key,value) published[key]=noctalia.json.decode(noctalia.json.encode(value)) end},
 runAsync=function(args,cb) table.insert(pending,{args=args,finish=cb}) end,
},{__index=noctalia})
function take(verb)
 for i,r in ipairs(pending) do if r.args[2]==verb then table.remove(pending,i);return r end end
 error('missing request '..verb)
end
function count(verb)
 local n=0;for _,r in ipairs(pending) do if r.args[2]==verb then n+=1 end end;return n
end
function option(args,name)
 for i,v in ipairs(args) do if v==name then return args[i+1] or true end end
end
function complete(request,data,exit)
 request.finish({exitCode=exit or 0,stdout=noctalia.json.encode(data),stderr='offline'})
end
)");
    load(host, "service.luau");
    run(host, "=home-generation", R"(
local oldHome=take('home')
complete(take('source'),{provider='ytmusic'})
local currentHome=take('home')
assert(option(currentHome.args,'--provider')=='ytmusic')
complete(currentHome,{shelves={{title='Current',items={}}},next='current-cursor'})
complete(oldHome,{shelves={{title='Obsolete',items={}}},next='old-cursor'})
assert(published['tmus.home'].shelves[1].title=='Current')
onIpc('home-more','current-cursor')
local oldMore=take('home-more')
onIpc('home-refresh')
local refresh=take('home')
assert(option(refresh.args,'--refresh'))
complete(refresh,{shelves={{title='Refreshed',items={}}},next='fresh-cursor'})
complete(oldMore,{shelves={{title='Obsolete page',items={}}}})
assert(#published['tmus.home'].shelves==1 and published['tmus.home'].shelves[1].title=='Refreshed')
onIpc('refresh')
local staleSource=take('source')
onIpc('source','soundcloud')
complete(staleSource,{provider='ytmusic'})
assert(published.tmus.source=='soundcloud')
)");
    run(host, "=liked-refresh", R"(
onIpc('tracks','local:liked')
local initial=take('library-tracks')
assert(published.tmus.tracksLoading)
onIpc('tracks-refresh','local:liked')
assert(count('library-tracks')==0)
complete(initial,{{id='kept',provider='local',title='Saved'}})
assert(not published.tmus.tracksLoading and published.tmus.tracks[1].id=='kept')
onIpc('tracks-refresh','unrelated:playlist')
assert(count('library-tracks')==0)
onIpc('tracks-refresh','local:liked')
local refresh=take('library-tracks')
assert(option(refresh.args,'--refresh') and published.tmus.tracksLoading)
complete(refresh,{},1)
assert(not published.tmus.tracksLoading and published.tmus.tracksError=='offline')
assert(published.tmus.tracks[1].id=='kept')
)");
  }
  {
    scripting::PluginBindingContext context;
    scripting::ScriptSettings settings;
    LuauHost host(api, "q/tmus:panel");
    bind(host, context, settings);
    run(host, "=panel-fixtures", R"(
requests={};watchers={}
fixture={linked=true,status='stopped',source='all',volume=20,queue={},playlists={},providers={},ratings={},tracks={},
 searchSerial=0,results={{id='one',provider='local',title='Track',kind='track'}}}
noctalia=setmetatable({
 nowMs=function() return 1000 end,
 clipboardText=function() return ' X\nY' end,
 runAsync=function(args) table.insert(requests,args) end,
 state={get=function(key)
  if key=='tmus' then return fixture end
  if key=='tmus.home' then return {status='ok',shelves={}} end
 end,watch=function(key,cb) watchers[key]=cb end},
},{__index=noctalia})
function actions(event)
 local found={};for _,args in ipairs(requests) do if args[6]==event then table.insert(found,args) end end;return found
end
panel=setmetatable({close=function() end,setNeedsFrameTick=function() end},{__index=panel})
)");
    run(host, "=ui-prelude", scripting::kUiPrelude);
    load(host, "panel.luau");
    run(host, "=search-keyboard", R"(
onOpen();submitSearch('seed');requests={}
onKey('I',true);onKey('A',true);onKey('Shift+A',true);onKey('Shift+B',true);onKey('Shift+exclam',true)
onKey('Return',true)
local sent=actions('search')
assert(#sent==1 and sent[1][7]=='all|'..noctalia.string.urlEncode('seedaAB!'))
fixture.searchSerial=1;watchers.tmus(fixture)
onKey('Return',true)
assert(#actions('play-context')==0 and #actions('play')==0)
onKey('Return',false)
onKey('I',true);onKey('ctrl+v',true);onKey('ctrl+v',true);onKey('ctrl+v',false)
onKey('Return',true)
sent=actions('search')
assert(#sent==2 and sent[2][7]=='all|'..noctalia.string.urlEncode('seedaAB! X Y'))
onClose()
)");
  }
  return failures == 0 ? 0 : 1;
}
