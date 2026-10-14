#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/scene/rect_node.h"
#include "scripting/luau_host.h"
#include "scripting/plugin_bindings.h"
#include "scripting/script_api_context.h"
#include "scripting/ui_prelude.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/ui_tree_reconciler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <variant>
#include <vector>

// Font-independent geometry fixture, as in ui_tree_reconciler_test. No GPU,
// compositor, live configuration or image loading is needed for the readouts.
std::unique_ptr<TextureManager> createDefaultTextureManager() { return nullptr; }

class MonitorLayoutRenderer : public Renderer {
public:
  TextMetrics measureText(
      std::string_view text, float fontSize, FontWeight, float, int, TextAlign, std::string_view, TextEllipsize, bool
  ) override {
    return TextMetrics{.width = static_cast<float>(text.size()) * fontSize * 0.5F, .bottom = fontSize};
  }
  TextMetrics measureFont(float fontSize, FontWeight) override { return TextMetrics{.bottom = fontSize}; }
  void measureTextCursorStops(
      std::string_view, float, const std::vector<std::size_t>&, std::vector<float>&, FontWeight
  ) override {}
  void measureTextCursorStopsWrapped(
      std::string_view, float, const std::vector<std::size_t>&, float, std::vector<TextCursorStop>&, FontWeight
  ) override {}
  TextMetrics measureGlyph(char32_t, float fontSize) override {
    return TextMetrics{.width = fontSize, .bottom = fontSize};
  }
  TextureManager& textureManager() override { std::abort(); }
  [[nodiscard]] float renderScale() const noexcept override { return 1.0F; }
};

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const std::filesystem::path directory(argv[1]);
  std::ifstream file(directory / "music-preview.luau");
  const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  scripting::ScriptApiContext api;
  auto inactive = std::make_shared<const toml::table>(toml::parse("[plugins]\nenabled=['q/skin-runtime']\n"));
  api.setConfigSnapshot(inactive);
  scripting::PluginBindingContext context;
  scripting::ScriptSettings settings;
  LuauHost host(api, "q/skin-runtime:music-preview");
  context.ownerId = host.runtimeName();
  context.host = &host;
  context.settings = &settings;
  host.setPluginDir(directory);
  host.setScriptContext(&context);
  scripting::registerPluginBindings(host.state(), &context);
  int watch = 0;
  host.setStateWatchHandler([&](std::string key, int ref) {
    if (key == "skin.status")
      watch = ref;
  });
  if (!host.exec("=clock", R"(
testClock = 1000
noctalia = setmetatable({nowMs = function() return testClock end}, {__index = noctalia})
)")
      || !host.exec("=ui-prelude", scripting::kUiPrelude)
      || !host.exec((directory / "music-preview.luau").string(), source)) {
    std::println(stderr, "music preview failed to load: {}", host.lastError());
    return 1;
  }
  int failures = 0;
  const auto check = [&](bool condition, const char* why) {
    if (!condition) {
      ++failures;
      std::println(stderr, "music_preview_widget: {} ({})", why, host.lastError());
    }
  };
  check(
      host.exec("=metric-boundaries", R"(
local metrics = require("./shared/bar-metrics.luau")
local cold = metrics.readouts(nil, nil)
assert(cold[1] == "CPU —" and cold[3] == "RAM —" and cold[6] == "DF —")
local values = metrics.readouts({
  sampledAtMs = 1000, cpu = {usagePercent = 0, tempC = 53.2},
  gpu = {usagePercent = 7}, ram = {usedMb = 12800, totalMb = 32768}
}, {usagePercent = 62.3})
assert(values[1] == "CPU 0%" and values[2] == "CPU 53°C")
assert(values[3] == "RAM 12.5/32.0 GiB" and values[4] == "GPU 7%")
assert(values[5] == "GPU —" and values[6] == "DF 62%")
assert(metrics.readouts({cpu = {usagePercent = 0}}, nil)[1] == "CPU —")
)"),
      "metric availability/units boundaries failed"
  );
  check(context.patch.uiTree.has_value() && context.patch.uiTree->children.empty(), "inactive recipe rendered music");
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse(R"(
[plugins]
enabled=['q/skin-runtime']
[plugin_settings.'q/skin-runtime']
family='kumihimo'
applied_variant='11111111111111111111111111111111'
)")));
  context.patch = {};
  check(host.callGlobal("update"), "activation update failed");
  check(context.patch.uiTree.has_value(), "activation did not render a bar element");
  if (context.patch.uiTree) {
    const auto it = context.patch.uiTree->props.find("visible");
    check(
        it != context.patch.uiTree->props.end() && std::get<bool>(it->second),
        "activation did not reveal the previously hidden retained frame"
    );
  }
  const auto deliver = [&](const std::string& music) {
    context.patch = {};
    check(
        watch != 0
            && host.callStateWatchCallback(
                watch, "{\"ok\":true,\"status\":{\"music\":" + music + "}}", std::chrono::milliseconds(50)
            ),
        "real status bridge transition failed"
    );
  };
  deliver(
      R"({"linked":true,"status":"playing","title":"First track","artist":"Artist A","artPath":"/tmp/first-cover.png"})"
  );
  const auto child = [&](const std::string& key) -> const ui::UiTreeNode* {
    if (!context.patch.uiTree)
      return nullptr;
    const auto find = [&](const auto& self, const ui::UiTreeNode& root) -> const ui::UiTreeNode* {
      if (root.key == key)
        return &root;
      for (const auto& node : root.children)
        if (const auto* found = self(self, node))
          return found;
      return nullptr;
    };
    return find(find, *context.patch.uiTree);
  };
  check(
      child("track-title") && std::get<std::string>(child("track-title")->props.at("text")) == "First track",
      "real title was not rendered"
  );
  check(
      child("track-cover") && std::get<std::string>(child("track-cover")->props.at("path")) == "/tmp/first-cover.png",
      "real cover was not rendered"
  );
  check(
      child("mist") && !std::get<bool>(child("mist")->props.at("visible")),
      "silence rendered a straight progress-like line"
  );
  context.patch = {};
  check(
      host.exec("=audio-frames", R"(
for frame = 1, 4 do
  onAudioSpectrum(string.rep('0.3,', 23) .. '0.3', '1,1')
  testClock += 80
end
)"),
      "real host spectrum frame failed"
  );
  check(
      child("mist") && std::get<bool>(child("mist")->props.at("visible")),
      "nonzero real audio did not reveal the envelope"
  );
  context.patch = {};
  check(host.exec("=fan-bay-geometry", "onBarGeometry(1568, 64, 0, 112)"), "compact fan bay geometry failed");
  const auto edge = [](const ui::UiTreeNode& node) {
    return std::get<double>(node.props.at("x")) + std::get<double>(node.props.at("width"));
  };
  const auto* glass = child("rain-glass");
  const auto* waves = child("mist");
  const auto* bay = child("tray-wood-bay");
  check(
      glass
          && waves
          && bay
          && edge(*glass) <= std::get<double>(bay->props.at("x"))
          && edge(*waves) <= std::get<double>(bay->props.at("x"))
          && std::get<double>(bay->props.at("width")) >= 44,
      "audio glass/waves overlap the fan's dedicated wood bay"
  );
  check(host.exec("=left-ai-reservation", "onBarGeometry(1568, 64, 700, 112)"), "expanded start lane geometry failed");
  check(
      child("rain-glass") && std::get<double>(child("rain-glass")->props.at("x")) >= 700,
      "rain glass covered the independent AI/clock start lane"
  );
  deliver(
      R"({"linked":true,"status":"paused","title":"Second track","artist":"Artist B","artPath":"/tmp/second-cover.png"})"
  );
  check(
      child("track-title") && std::get<std::string>(child("track-title")->props.at("text")) == "Second track",
      "track change retained the old title"
  );
  check(child("mist") && !std::get<bool>(child("mist")->props.at("visible")), "paused audio still animated");
  deliver("null");
  check(
      child("track-title") && std::get<std::string>(child("track-title")->props.at("text")) == "No player",
      "missing player kept a stale title"
  );
  check(
      child("track-cover") == nullptr && child("empty-cover") != nullptr, "missing player kept another track's cover"
  );
  // The icon-only readouts must retain units, real zeroes and unavailable
  // states; tooltips must identify the readings after visible names are removed.
  check(
      host.exec("=monitor-fixture", R"(
monitorStats = {
  sampledAtMs = 1000, cpu = {usagePercent = 0, tempC = 53.2},
  gpu = {usagePercent = 7}, ram = {usedMb = 12800, totalMb = 32768}
}
monitorDisk = {usagePercent = 62.3}
noctalia = setmetatable({
  systemStats = function() return monitorStats end,
  diskStats = function() return monitorDisk end,
}, {__index = noctalia})
)"),
      "monitor fixture failed"
  );
  std::ifstream widgetFile(directory / "widget.luau");
  const std::string widgetSource{std::istreambuf_iterator<char>(widgetFile), std::istreambuf_iterator<char>()};
  context.patch = {};
  check(host.exec((directory / "widget.luau").string(), widgetSource), "monitor widget failed to load");
  const auto readout = [&](int index, const char* expected) {
    const auto* node = child("metric-" + std::to_string(index));
    check(
        node && std::get<std::string>(node->props.at("text")) == expected,
        "monitor lost a value, unit or unavailable state"
    );
  };
  readout(1, "0%");
  readout(2, "53°C");
  readout(3, "12.5 GiB");
  readout(4, "7%");
  readout(5, "—");
  readout(6, "62%");
  for (const char* key : {"left-fitting-space"}) {
    const auto* tree = child(key);
    check(tree != nullptr, "bar spacing disappeared");
    if (tree == nullptr)
      continue;
    MonitorLayoutRenderer renderer;
    Flex layout;
    ui::UiTreeReconciler reconciler;
    reconciler.reconcile(layout, *tree, renderer);
    layout.layout(renderer);
    const auto checkPaint = [&](const auto& self, const Node& node) -> void {
      if (const auto* rect = dynamic_cast<const RectNode*>(&node)) {
        check(rect->style().fill.a == 0.0F, "empty bar spacing painted an opaque rectangle");
      }
      for (const auto& nested : node.children())
        self(self, *nested);
    };
    checkPaint(checkPaint, layout);
  }
  const auto monitorWidth = [&](float scale) {
    const auto* tree = child("metrics-content");
    check(tree != nullptr, "monitor has no content-sized readout container");
    if (tree == nullptr)
      return 0.0F;
    MonitorLayoutRenderer renderer;
    Flex layout;
    ui::UiTreeReconciler reconciler;
    reconciler.setScale(scale);
    reconciler.reconcile(layout, *tree, renderer);
    layout.layout(renderer);
    float left = layout.width(), right = 0.0F;
    const auto bounds = [&](const auto& self, const Node& node) -> void {
      if (dynamic_cast<const Label*>(&node) || dynamic_cast<const Glyph*>(&node)) {
        float x = 0.0F, y = 0.0F;
        Node::mapToScene(&node, 0.0F, 0.0F, x, y);
        left = std::min(left, x);
        right = std::max(right, x + node.width());
      } else {
        for (const auto& nested : node.children())
          self(self, *nested);
      }
    };
    bounds(bounds, layout);
    // Flex rounds each nested origin to pixels; fractional scaling can move
    // the column and its label by up to half a pixel each.
    const float tolerance = scale == 1.0F ? 0.01F : 1.01F;
    const bool symmetric =
        std::abs(left - 6.0F * scale) < tolerance && std::abs(layout.width() - right - 6.0F * scale) < tolerance;
    if (!symmetric) {
      std::println(
          stderr, "monitor geometry: scale={}, width={}, left={}, right={}", scale, layout.width(), left,
          layout.width() - right
      );
    }
    check(symmetric, "monitor readouts do not have symmetric six-pixel horizontal insets");
    return layout.width();
  };
  const float normalWidth = monitorWidth(1.0F);
  monitorWidth(1.25F);
  const auto tooltip = [&](const char* key, const char* value) {
    if (context.patch.tooltip) {
      for (const auto& row : context.patch.tooltip->rows) {
        if (row.key == key && row.value == value)
          return true;
      }
    }
    return false;
  };
  check(
      tooltip("CPU temperature", "53°C")
          && tooltip("GPU temperature", "—")
          && tooltip("RAM used / total", "12.5/32.0 GiB")
          && tooltip("Disk / usage", "62%"),
      "icon-only readings lost their full tooltip identification"
  );
  context.patch = {};
  check(
      host.exec(
          "=monitor-wide-memory", "monitorStats.ram.usedMb = 131072; monitorStats.ram.totalMb = 262144; update()"
      ),
      "wide memory transition failed"
  );
  readout(3, "128.0 GiB");
  check(monitorWidth(1.0F) > normalWidth, "monitor did not grow with a longer real memory readout");
  context.patch = {};
  check(
      host.exec("=monitor-unavailable", "monitorStats = nil; monitorDisk = nil; update()"),
      "monitor unavailable transition failed"
  );
  for (int index = 1; index <= 6; ++index)
    readout(index, "—");
  check(monitorWidth(1.0F) < normalWidth, "unavailable readings retained the empty right-hand reserve");
  check(child("ai-value") == nullptr, "monitor retained an embedded AI hitbox");
  check(
      host.exec("=clock-format-contract", R"(
noctalia = setmetatable({
  timeFormat=function() return '{:%H:%M}' end,
  dateFormat=function() return '{:%A, %x}' end,
  formatTime=function(format, ...)
    assert(select('#', ...)==0, 'gsub count became an epoch timestamp')
    return format=='%H:%M' and '14:32' or format=='%d.%m' and '12.10' or 'Monday, 12 October'
  end,
}, {__index=noctalia})
)"),
      "clock fixture failed"
  );
  std::ifstream clockFile(directory / "bar-clock.luau");
  const std::string clockSource{std::istreambuf_iterator<char>(clockFile), std::istreambuf_iterator<char>()};
  check(host.exec((directory / "bar-clock.luau").string(), clockSource), "independent clock failed to load");
  check(
      child("bar-clock-time")
          && std::get<std::string>(child("bar-clock-time")->props.at("text")) == "14:32"
          && child("bar-clock-date")
          && std::get<std::string>(child("bar-clock-date")->props.at("text")) == "12.10",
      "moving the clock lost the actual date/time or forwarded an epoch timestamp"
  );
  return failures == 0 ? 0 : 1;
}
