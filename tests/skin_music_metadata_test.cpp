#include "config/config_service.h"
#include "scripting/plugin_registry.h"
#include "scripting/plugin_state_store.h"
#include "shell/panel/panel_manager.h"
#include "shell/skins/skin_service.h"
#include "tests/test_check.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const auto root = std::filesystem::temp_directory_path() / ("noctalia-music-metadata-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "config/noctalia");
  std::ofstream(root / "config/noctalia/config.toml") << "[plugins]\nenabled = ['q/tmus']\n";
  ::setenv("NOCTALIA_CONFIG_HOME", (root / "config").c_str(), 1);
  ::setenv("NOCTALIA_STATE_HOME", (root / "state").c_str(), 1);
  auto& registry = scripting::PluginRegistry::instance();
  registry.setSources({argv[1]});
  registry.setEnabledFilter(std::unordered_set<std::string>{"q/tmus"});
  registry.ensureScanned();
  {
    ConfigService config;
    PanelManager panels;
    noctalia::shell::skins::SkinService skin(config, panels);
    auto& state = scripting::PluginStateStore::instance();
    state.set("q/tmus", "tmus", R"({"title":"Track","artist":"Artist","album":"Actual album","linked":true})");
    const auto music = nlohmann::json::parse(skin.statusJson())["music"];
    TEST_CHECK(music.is_object());
    TEST_CHECK(music.value("album", "") == "Actual album");
    for (const auto* payload : {R"({"title":"Next track","linked":true})",
                                R"({"title":"Next track","album":42,"linked":true})"}) {
      state.set("q/tmus", "tmus", payload);
      TEST_CHECK(!nlohmann::json::parse(skin.statusJson())["music"].contains("album"));
    }
  }
  std::filesystem::remove_all(root);
  return 0;
}
