#include "config/config_service.h"
#include "pipewire/pipewire_service.h"
#include "pipewire/pipewire_spectrum.h"
#include "render/scene/fancy_audio_visualizer_node.h"
#include "shell/desktop/desktop_widget_factory.h"
#include "shell/desktop/desktop_widget_settings_registry.h"
#include "test_check.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
  FancyAudioVisualizerNode* visual(Node* node) {
    if (auto* found = dynamic_cast<FancyAudioVisualizerNode*>(node))
      return found;
    if (node != nullptr)
      for (const auto& child : node->children())
        if (auto* found = visual(child.get()))
          return found;
    return nullptr;
  }
} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / ("noctalia-audio-presets-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "config/noctalia");
  std::filesystem::create_directories(root / "runtime");
  std::ofstream(root / "config/noctalia/config.toml") << "[desktop_widgets]\nenabled = true\n";
  ::setenv("NOCTALIA_CONFIG_HOME", (root / "config").c_str(), 1);
  ::setenv("NOCTALIA_STATE_HOME", (root / "state").c_str(), 1);
  ::setenv("PIPEWIRE_RUNTIME_DIR", (root / "runtime").c_str(), 1);
  ::setenv("PIPEWIRE_REMOTE", "noctalia-test-no-daemon", 1);
  PipeWireService audio;
  PipeWireSpectrum spectrum(audio);
  DesktopWidgetFactory factory({.pipewireSpectrum = &spectrum});
  constexpr std::array types{"audio_ribbons",   "audio_contour", "audio_radial_bars",
                             "audio_wave_ring", "audio_mist",    "audio_ornament"};
  constexpr std::array modes{FancyAudioVisualizerMode::Ribbons, FancyAudioVisualizerMode::Contour,
                             FancyAudioVisualizerMode::Bars,    FancyAudioVisualizerMode::Wave,
                             FancyAudioVisualizerMode::Mist,    FancyAudioVisualizerMode::Ornament};
  const auto& available = desktop_settings::desktopWidgetTypeSpecs();
  DesktopWidgetsConfig saved;
  for (std::size_t i = 0; i < types.size(); ++i) {
    TEST_CHECK(std::ranges::any_of(available, [&](const auto& entry) { return entry.type == types[i]; }));
    auto widget = factory.create(types[i], {});
    TEST_CHECK(widget != nullptr);
    widget->create();
    auto* node = visual(widget->root());
    TEST_CHECK(node != nullptr && node->style().mode == modes[i]);
    TEST_CHECK(!widget->hasBackground());
    const auto controls = desktop_settings::desktopWidgetSettingSpecs(types[i]);
    TEST_CHECK(std::ranges::none_of(controls, [](const auto& spec) {
      return spec.schema.key == "visualization_mode";
    }));
    DesktopWidgetState state;
    state.id = "visual-" + std::to_string(i);
    state.type = types[i];
    state.outputName = "DP-TEST";
    state.cx = 200.0F + static_cast<float>(i) * 20.0F;
    state.cy = 180.0F;
    state.boxWidth = 400.0F;
    state.boxHeight = 180.0F;
    state.settings = {{"sensitivity", 1.2 + static_cast<double>(i) * 0.1}, {"primary_color", std::string("#ff3366")}};
    saved.widgets.push_back(std::move(state));
  }
  auto customized =
      factory.create("audio_ribbons", {{"sensitivity", 2.3}, {"visualization_mode", std::string("rings")}});
  auto independent = factory.create("audio_ribbons", {});
  TEST_CHECK(customized != nullptr && independent != nullptr);
  customized->create();
  independent->create();
  const auto customStyle = visual(customized->root())->style();
  TEST_CHECK(customStyle.mode == FancyAudioVisualizerMode::Ribbons && customStyle.sensitivity == 2.3F);
  TEST_CHECK(visual(independent->root())->style().sensitivity != customStyle.sensitivity);

  DesktopWidgetState legacy;
  legacy.id = "existing-customized-circle";
  legacy.type = "fancy_audio_visualizer";
  legacy.settings = {
      {"visualization_mode", std::string("wave_rings")},
      {"sensitivity", 2.1},
      {"rotation_speed", 1.2},
      {"wave_thickness", 1.7},
      {"primary_color", std::string("#123456")},
      {"fade_when_idle", false}
  };
  saved.widgets.push_back(legacy);
  {
    ConfigService config;
    TEST_CHECK(config.setDesktopWidgetsState(saved));
  }
  {
    ConfigService reloaded;
    TEST_CHECK(reloaded.config().desktopWidgets.widgets == saved.widgets);
    for (std::size_t i = 0; i < types.size(); ++i) {
      const auto& state = reloaded.config().desktopWidgets.widgets[i];
      auto widget = factory.create(state.type, state.settings);
      TEST_CHECK(widget != nullptr);
      widget->create();
      TEST_CHECK(visual(widget->root())->style().mode == modes[i]);
    }
    const auto& old = reloaded.config().desktopWidgets.widgets.back();
    auto widget = factory.create(old.type, old.settings);
    TEST_CHECK(widget != nullptr);
    widget->create();
    const auto& style = visual(widget->root())->style();
    TEST_CHECK(style.mode == FancyAudioVisualizerMode::WaveRings);
    TEST_CHECK(style.sensitivity == 2.1F && style.rotationSpeed == 1.2F && style.waveThickness == 1.7F);
  }
  std::filesystem::remove_all(root);
  return 0;
}
