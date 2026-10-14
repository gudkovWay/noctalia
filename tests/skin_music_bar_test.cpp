#include "config/config_service.h"
#include "scripting/plugin_registry.h"
#include "shell/panel/panel_manager.h"
#include "shell/skins/skin_service.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const auto root = std::filesystem::temp_directory_path() / ("noctalia-music-bar-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "config/noctalia");
  std::ofstream(root / "config/noctalia/config.toml") << "[plugins]\nenabled = ['q/skin-runtime', 'q/ai-triage']\n";
  ::setenv("NOCTALIA_CONFIG_HOME", (root / "config").c_str(), 1);
  ::setenv("NOCTALIA_STATE_HOME", (root / "state").c_str(), 1);
  scripting::PluginRegistry::instance().setSources({argv[1]});
  scripting::PluginRegistry::instance().setEnabledFilter(
      std::unordered_set<std::string>{"q/skin-runtime", "q/ai-triage"}
  );
  scripting::PluginRegistry::instance().ensureScanned();
  int failures = 0;
  auto check = [&](bool condition, const char* why) {
    if (!condition) {
      ++failures;
      std::println(stderr, "skin_music_bar: {}", why);
    }
  };
  const auto aiPanel = scripting::PluginRegistry::instance().resolve("q/ai-triage:triage");
  check(
      aiPanel.has_value()
          && aiPanel->entry->panelPlacementDefault == "attached"
          && aiPanel->entry->panelOpenNearClickDefault,
      "AI manifest does not attach to its actual bar opener"
  );
  {
    ConfigService config;
    PanelManager panels;
    noctalia::shell::skins::SkinService skin(config, panels);
    const std::string recipe =
        R"({"schema_version":1,"target":"noctalia","variant_id":"11111111111111111111111111111111","content_id":"2222222222222222222222222222222222222222222222222222222222222222","name":"Music bar","family":"kumihimo","topology":"hanging-tags","density":"comfortable","preview_path":"/tmp/music-bar-preview.webp","colors":{"surface":"#241c1d","surface_variant":"#302326","text":"#e7d7bc","muted":"#b5a38f","accent":"#c28c59","outline":"#70514b"}})";
    const auto applied = nlohmann::json::parse(skin.applyJson(recipe));
    check(applied.value("ok", false), "native recipe apply failed");
    const auto bar = [&]() -> const BarConfig* {
      const auto& bars = config.config().bars;
      const auto it = std::ranges::find(bars, "skin-runtime-status", &BarConfig::name);
      return it == bars.end() ? nullptr : &*it;
    };
    check(
        bar() != nullptr && bar()->centerWidgets == std::vector<std::string>{"q/skin-runtime:music-preview"},
        "music did not occupy the actual bar center lane"
    );
    const std::vector<std::string> start{
        "q/skin-runtime:skin-widget", "q/ai-triage:ai-triage-widget", "q/skin-runtime:bar-clock"
    };
    check(bar() && bar()->startWidgets == start, "independent AI is not immediately after monitoring");
    check(!panels.isOpenPanel("q/skin-runtime:music"), "bar preview opened a desktop music panel");
    check(skin.isActiveProfileValidated(), "new bar receipt is not active");
    const auto tray = config.config().widgets.find("q_skin_runtime_tray");
    check(
        bar() && tray != config.config().widgets.end() && !resolveWidgetBarCapsuleSpec(*bar(), &tray->second).enabled,
        "joinery bar retained a separate tray capsule"
    );
    // Model a real v1 compact receipt: its center was empty, not user content.
    auto receipt = nlohmann::json::parse(*config.stateString("skin_runtime", "active"));
    receipt["bar"]["geometry_revision"] = 1;
    check(config.setStateString("skin_runtime", "active", receipt.dump()), "could not seed old receipt");
    check(
        config.setOverride(
            {"bar", "skin-runtime-status", "start"}, std::vector<std::string>{"q/skin-runtime:skin-widget"}
        ),
        "could not seed historical monitor/embedded-AI start lane"
    );
    check(
        config.setOverrides(
            {{{"bar", "skin-runtime-status", "center"}, std::vector<std::string>{}},
             {{"widget", "q_skin_runtime_tray", "capsule"}, true}}
        ),
        "could not seed old center lane"
    );
    check(skin.isActiveProfileValidated(), "old compact receipt stopped validating before migration");
    check(nlohmann::json::parse(skin.applyJson(recipe)).value("ok", false), "same-recipe migration failed");
    check(
        bar() != nullptr && bar()->centerWidgets == std::vector<std::string>{"q/skin-runtime:music-preview"},
        "migration did not install centered music"
    );
    check(skin.isActiveProfileValidated(), "migrated receipt is not active");
    receipt = nlohmann::json::parse(*config.stateString("skin_runtime", "active"));
    receipt["bar"]["geometry_revision"] = 3;
    check(config.setStateString("skin_runtime", "active", receipt.dump()), "could not seed joinery receipt");
    check(
        config.setOverride(
            {"bar", "skin-runtime-status", "start"}, std::vector<std::string>{"q/skin-runtime:skin-widget"}
        ),
        "could not seed old joinery lane"
    );
    check(skin.isActiveProfileValidated(), "revision 3 was reinterpreted as the new AI lane");
    check(nlohmann::json::parse(skin.applyJson(recipe)).value("ok", false), "joinery cassette migration failed");
    check(bar() && bar()->startWidgets == start, "joinery migration misplaced independent AI");
    check(nlohmann::json::parse(skin.clearJson()).value("ok", false), "native recipe clear failed");
    check(bar() == nullptr, "clear left behind the old empty center as foreign content");
    check(!config.hasOverride({"bar", "default", "enabled"}), "clear did not restore default bar ownership");
  }
  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
