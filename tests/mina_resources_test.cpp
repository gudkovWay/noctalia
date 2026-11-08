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
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse("[plugins]\nenabled=['q/skin-runtime']\n")));
  scripting::ScriptSettings settings;
  scripting::PluginBindingContext context;
  LuauHost host(api, "q/skin-runtime:mina-resources");
  context.ownerId = host.runtimeName();
  context.host = &host;
  context.settings = &settings;
  host.setPluginDir(directory);
  host.setScriptContext(&context);
  scripting::registerPluginBindings(host.state(), &context);
  int failures = 0;
  const auto check = [&](bool value, const char* reason) {
    if (!value) {
      ++failures;
      std::println(stderr, "mina_resources: {} ({})", reason, host.lastError());
    }
  };
  std::ifstream input(directory / "mina-resources.luau");
  const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (source.empty()) {
    std::println(stderr, "mina_resources: resource reveal implementation is missing");
    return 1;
  }
  check(
      host.exec("=host-data-boundary", R"(
testStats=nil
testDisk=nil
noctalia=setmetatable({
 systemStats=function() return testStats end,
 diskStats=function(path) assert(path=='/'); return testDisk end,
}, {__index=noctalia})
)"),
      "host data fixture rejected"
  );
  check(host.exec("=ui", scripting::kUiPrelude), "native UI prelude rejected");
  check(host.exec("=mina-resources", source), "resource panel script rejected");
  const auto node = [&](const std::string& key) -> const ui::UiTreeNode* {
    if (!context.patch.uiTree)
      return nullptr;
    const auto find = [&](const auto& self, const ui::UiTreeNode& tree) -> const ui::UiTreeNode* {
      if (tree.key == key)
        return &tree;
      for (const auto& child : tree.children)
        if (const auto* result = self(self, child))
          return result;
      return nullptr;
    };
    return find(find, *context.patch.uiTree);
  };
  const auto text = [&](const char* key) {
    const auto* value = node(key);
    if (value == nullptr)
      return std::string{};
    const auto it = value->props.find("text");
    return it != value->props.end() && std::holds_alternative<std::string>(it->second)
        ? std::get<std::string>(it->second)
        : std::string{};
  };
  check(host.exec("=open-missing", "onPanelGeometry(593,960,-1,'left'); onOpen('')"), "missing sample open failed");
  for (const auto* key :
       {"mina-resource-cpu-load", "mina-resource-cpu-temperature", "mina-resource-gpu-load",
        "mina-resource-gpu-temperature", "mina-resource-ram-used", "mina-resource-disk-used"})
    check(text(key) == "—", "missing host reading became zero or a stale sample");
  check(
      host.exec("=sample", R"(
testStats={sampledAtMs=1000,cpu={usagePercent=26,tempC=61},gpu={usagePercent=66,tempC=44},ram={usedMb=12595.2,totalMb=32768,usagePercent=38.4375}}
testDisk={usagePercent=71,totalBytes=1000000,freeBytes=290000,availableBytes=290000}
update()
)"),
      "sample update failed"
  );
  check(text("mina-resource-cpu-load") == "26%", "CPU load did not come from host sample");
  check(text("mina-resource-cpu-temperature") == "61°C", "CPU temperature lost units or source");
  check(text("mina-resource-gpu-load") == "66%", "GPU load did not come from host sample");
  check(text("mina-resource-gpu-temperature") == "44°C", "GPU temperature lost units or source");
  check(text("mina-resource-ram-used") == "12.3 / 32.0 GiB", "RAM capacity was fabricated or formatted incorrectly");
  check(text("mina-resource-disk-used") == "71%", "root disk reading was not rendered");
  check(node("mina-resource-material") != nullptr, "resource reveal lost the shared wing material");
  check(
      host.exec("=malformed", R"(
testStats={sampledAtMs=2000,cpu={usagePercent=0/0,tempC=math.huge},gpu={},ram={usedMb=-1,totalMb=0}}
testDisk={usagePercent=0/0}
update()
)"),
      "malformed host sample crashed the reveal"
  );
  for (const auto* key :
       {"mina-resource-cpu-load", "mina-resource-cpu-temperature", "mina-resource-gpu-load",
        "mina-resource-gpu-temperature", "mina-resource-ram-used", "mina-resource-disk-used"})
    check(text(key) == "—", "invalid host reading appeared as a valid metric");
  check(host.exec("=close", "onClose()"), "close failed");
  context.patch = {};
  check(host.exec("=closed-update", "update()"), "closed panel update failed");
  check(!context.patch.uiTree.has_value(), "closed resource reveal continued rendering");
  return failures == 0 ? 0 : 1;
}
