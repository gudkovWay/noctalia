#include "scripting/luau_host.h"
#include "scripting/plugin_bindings.h"
#include "scripting/script_api_context.h"
#include "scripting/ui_prelude.h"
#include "render/core/image_file_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const std::filesystem::path directory(argv[1]);
  scripting::ScriptApiContext api;
  api.setConfigSnapshot(std::make_shared<const toml::table>(toml::parse("[plugins]\nenabled=['q/skin-runtime']\n")));
  scripting::ScriptSettings settings;
  scripting::PluginBindingContext context;
  LuauHost host(api, "q/skin-runtime:mina-music");
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
  std::ifstream input(directory / "mina-music.luau");
  const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (source.empty()
      || !host.exec("=clock-and-outbound-boundary", R"(
testClock=1000
testCalls={}
testPanels={}
noctalia=setmetatable({
 nowMs=function() return testClock end,
 runAsync=function(argv) table.insert(testCalls,argv); return true end,
 togglePanel=function(id) table.insert(testPanels,id) end,
}, {__index=noctalia})
)")
      || !host.exec("=ui", scripting::kUiPrelude)
      || !host.exec("=mina-music", source)) {
    std::println(stderr, "mina music failed to load: {}", host.lastError());
    return 1;
  }
  int failures = 0;
  auto check = [&](bool condition, const char* reason) {
    if (!condition) {
      ++failures;
      std::println(stderr, "mina_music_widget: {} ({})", reason, host.lastError());
    }
  };
  for (const auto* name : {"sidebar-cloth.png", "tray-cloth.png"}) {
    const auto image = loadImageFile((directory / "skin-assets/mina" / name).string());
    check(image.has_value(), "Mina cloth cannot load through native image decoder");
    if (!image)
      continue;
    bool clearEdges = true;
    for (int x = 0; x < image->width; ++x)
      clearEdges = clearEdges && image->rgba[static_cast<std::size_t>(x) * 4 + 3] == 0
          && image->rgba[((static_cast<std::size_t>(image->height) - 1) * image->width + x) * 4 + 3] == 0;
    for (int y = 0; y < image->height; ++y)
      clearEdges = clearEdges && image->rgba[(static_cast<std::size_t>(y) * image->width + image->width - 1) * 4 + 3] == 0;
    check(clearEdges, "Mina garment is cut off at an outer PNG edge");
  }
  for (const auto* name : {"signal-controls.svg", "signal-tray.svg", "sidebar-frame.svg", "signal-music.svg",
                           "workspace-umbrella.svg", "mina-portrait.png"}) {
    const auto image = loadImageFile((directory / "skin-assets/mina" / name).string());
    check(image.has_value() && image->width > 0 && image->height > 0,
          "ornate frame cannot render through the native SVG decoder");
  }
  const auto rail = loadImageFile((directory / "skin-assets/mina/sidebar-frame.svg").string());
  const auto wing = loadImageFile((directory / "skin-assets/mina/tray-cloth.png").string());
  const auto aiCape = loadImageFile(
      (directory.parent_path() / "ai-triage/skin-assets/ai-cassette/cape.png").string());
  check(aiCape.has_value(), "AI cape cannot load through native image decoder");
  if (wing && aiCape) {
    const bool sharedScale = wing->width == 415 * 2 && aiCape->width == 900 * 2
        && wing->height == 671 * 2 && aiCape->height == wing->height;
    check(sharedScale, "tray and AI cape do not preserve a common physical material scale");
    if (sharedScale) {
      bool sameReturns = true;
      for (int y = 0; y < wing->height; ++y) {
        for (const bool right : {false, true}) {
          const int columns = (right ? 168 : 96) * 2;
          for (int x = 0; x < columns; ++x) {
            const int wingX = right ? wing->width - columns + x : x;
            const int aiX = right ? aiCape->width - columns + x : x;
            for (int channel = 0; channel < 4; ++channel)
              sameReturns = sameReturns
                  && wing->rgba[(static_cast<std::size_t>(y) * wing->width + wingX) * 4 + channel]
                      == aiCape->rgba[(static_cast<std::size_t>(y) * aiCape->width + aiX) * 4 + channel];
          }
        }
      }
      check(sameReturns, "tray and AI use different or stretched folds, bronze trim or crimson returns");
    }
  }
  const auto signalWing = loadImageFile((directory / "skin-assets/mina/signal-tray.svg").string());
  if (rail && signalWing) {
    const auto layout = toml::parse_file((directory / "mina-bars.toml").string());
    const int railWidth = layout["bar"]["mina-left"]["thickness"].value_or(0);
    const int wingX = railWidth - layout["bar"]["mina-left"]["panel_overlap"].value_or(0);
    const auto alpha = [](const auto& image, int x, int y, int width) {
      if (x < 0 || x >= width)
        return 0;
      const auto sourceX = static_cast<std::size_t>(x * image.width / width);
      const auto sourceY = static_cast<std::size_t>(y * image.height / 671);
      return static_cast<int>(image.rgba[(sourceY * image.width + sourceX) * 4 + 3]);
    };
    bool connected = railWidth > 0;
    for (const int y : {200, 240, 360, 480})
      for (int x = 68; x <= 96; ++x)
        connected = connected && (alpha(*rail, x, y, railWidth) >= 128
                                  || alpha(*signalWing, x - wingX, y, 415) >= 128);
    check(connected, "wallpaper is visible through the join between the support and opened wing");
    bool sameMaterial = true;
    for (const int y : {220, 300, 400, 480}) {
      const auto railOffset = (static_cast<std::size_t>(y * rail->height / 671) * rail->width
                               + 64 * rail->width / railWidth) * 4;
      const auto wingOffset = (static_cast<std::size_t>(y * signalWing->height / 671) * signalWing->width
                               + 8 * signalWing->width / 415) * 4;
      for (int channel = 0; channel < 3; ++channel) {
        const int a = rail->rgba[railOffset + channel];
        const int b = signalWing->rgba[wingOffset + channel];
        sameMaterial = sameMaterial && a - b <= 5 && b - a <= 5;
      }
    }
    check(sameMaterial, "sidebar and drawer no longer share one continuous cloth material");
  }
  const auto volumeEmpty = loadImageFile((directory / "skin-assets/mina/volume-lining.png").string());
  const auto volumeFull = loadImageFile((directory / "skin-assets/mina/volume-lining-fill.png").string());
  check(volumeEmpty && volumeFull, "dynamic volume material pair is missing");
  const auto redPixels = [](const auto& image) {
    int count = 0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4)
      if (image.rgba[i + 3] != 0 && image.rgba[i] > 70
          && image.rgba[i] > image.rgba[i + 1] * 1.65F
          && image.rgba[i] > image.rgba[i + 2] * 1.3F
          && image.rgba[i + 2] > image.rgba[i + 1] * 1.05F)
        ++count;
    return count;
  };
  if (volumeEmpty)
    check(redPixels(*volumeEmpty) == 0, "zero volume retains a baked red fill");
  if (volumeEmpty && volumeFull) {
    check(volumeEmpty->width == volumeFull->width && volumeEmpty->height == volumeFull->height,
          "volume fill does not align with the inactive material");
    check(redPixels(*volumeFull) > 200, "volume fill does not extend through the whole scale");
    for (const auto* image : {&*volumeEmpty, &*volumeFull}) {
      bool clearFrame = true;
      for (int y = 0; y < image->height; ++y)
        for (const int x : {0, 1, image->width - 2, image->width - 1})
          clearFrame = clearFrame && image->rgba[(static_cast<std::size_t>(y) * image->width + x) * 4 + 3] == 0;
      for (int x = 0; x < image->width; ++x)
        for (const int y : {0, image->height - 1})
          clearFrame = clearFrame && image->rgba[(static_cast<std::size_t>(y) * image->width + x) * 4 + 3] == 0;
      check(clearFrame, "volume material background covers the sidebar frame");
    }
    if (volumeEmpty->rgba.size() == volumeFull->rgba.size()) {
      bool sameAlpha = true;
      for (std::size_t i = 3; i < volumeEmpty->rgba.size(); i += 4)
        sameAlpha = sameAlpha && volumeEmpty->rgba[i] == volumeFull->rgba[i];
      check(sameAlpha, "volume fill changes the original material silhouette");
    }
  }
  const auto node = [&](const std::string& key) -> const ui::UiTreeNode* {
    if (!context.patch.uiTree)
      return nullptr;
    const auto find = [&](const auto& self, const ui::UiTreeNode& tree) -> const ui::UiTreeNode* {
      if (tree.key == key)
        return &tree;
      for (const auto& child : tree.children)
        if (const auto* match = self(self, child))
          return match;
      return nullptr;
    };
    return find(find, *context.patch.uiTree);
  };
  const auto deliver = [&](const char* music) {
    check(
        watch != 0
            && host.callStateWatchCallback(
                watch, std::string("{\"ok\":true,\"status\":{\"music\":") + music + "}}", std::chrono::milliseconds(50)
            ),
        "status delivery failed"
    );
  };
  check(host.exec("=geometry", "onBarGeometry(3336,180,0,0)"), "output geometry rejected");
  deliver(
      R"({"linked":true,"status":"playing","title":"Real track","artist":"Real artist","album":"Real album","artPath":"/tmp/art.png","position":84,"duration":232})"
  );
  check(
      node("mina-title") && std::get<std::string>(node("mina-title")->props.at("text")) == "Real track",
      "real title missing"
  );
  check(
      node("mina-cover") && std::get<std::string>(node("mina-cover")->props.at("path")) == "/tmp/art.png",
      "real art missing"
  );
  check(
      node("mina-progress") && std::get<double>(node("mina-progress")->props.at("progress")) > 0.36,
      "real progress missing"
  );
  check(node("mina-album") && std::get<std::string>(node("mina-album")->props.at("text")) == "Real album",
        "genuine album missing");
  for (const auto* key : {"mina-title", "mina-artist", "mina-album", "mina-time"})
    check(node(key) && std::get<std::string>(node(key)->props.at("fontFamily")) == "Maple Mono",
          "player text falls back to the rejected display font");
  for (const auto* key : {"mina-shuffle", "mina-repeat", "mina-toggle", "mina-previous", "mina-next", "mina-library",
                          "mina-spectrum-1"})
    check(node(key) == nullptr, "removed player quick action or spectrum bar remains visible");
  const double fixedX = node("mina-cover") ? std::get<double>(node("mina-cover")->props.at("x")) : -1;
  const double fixedY = node("mina-title") ? std::get<double>(node("mina-title")->props.at("y")) : -1;
  check(node("mina-cover") && !node("mina-cover")->props.contains("onClick"), "cover hides a removed library action");
  check(node("mina-lining") == nullptr, "player shows decorative waves before real audio arrives");
  check(
      host.exec("=signal", "for i=1,4 do testClock+=50; onAudioSpectrum(string.rep('0.6,',23)..'0.6') end"),
      "spectrum delivery failed"
  );
  const auto curve = [&]() {
    const auto* material = node("mina-lining");
    if (!material || !material->props.contains("values"))
      return std::vector<double>{};
    return std::get<std::vector<double>>(material->props.at("values"));
  };
  const auto firstCurve = curve();
  check(firstCurve.size() == 64, "real audio did not create smooth full-width folds");
  if (const auto* material = node("mina-lining"); material && material->props.contains("values")) {
    bool bounded = true;
    for (const auto* series : {"values", "values2"})
      for (double value : std::get<std::vector<double>>(material->props.at(series)))
        bounded = bounded && std::isfinite(value) && value >= 0 && value <= 1;
    check(bounded, "audio fold escaped its fixed drawing region");
    check(node("mina-cloth")
              && std::get<double>(material->props.at("x")) == std::get<double>(node("mina-cloth")->props.at("x")) + 6
              && std::get<double>(material->props.at("width")) == std::get<double>(node("mina-cloth")->props.at("width")) - 12,
          "audio folds retain horizontal gutters inside the frame");
  }
  check(host.exec("=steady-audio", "testClock+=250; onAudioSpectrum(string.rep('0.6,',23)..'0.6')"),
        "steady audio update failed");
  const auto nextCurve = curve();
  double motion = 0;
  if (firstCurve.size() == nextCurve.size())
    for (std::size_t i = 0; i < firstCurve.size(); ++i)
      motion = std::max(motion, std::abs(firstCurve[i] - nextCurve[i]));
  check(motion > 0.1, "steady audio leaves the folds effectively static");
  check(node("mina-cloth") && !node("mina-cloth")->props.contains("hem"), "audio deforms fixed frame");
  check(node("mina-cover") && std::get<double>(node("mina-cover")->props.at("x")) == fixedX
            && node("mina-title") && std::get<double>(node("mina-title")->props.at("y")) == fixedY,
        "audio moved cover or metadata");
  check(context.patch.uiTree
            && std::get<std::vector<double>>(context.patch.uiTree->props.at("inputRects"))
                == std::vector<double>({0, 0, 0, 0}),
        "passive player retains removed controls' input regions");
  check(host.exec("=silence", "for i=1,10 do testClock+=50; onAudioSpectrum(string.rep('0,',23)..'0') end"),
        "silence delivery failed");
  check(node("mina-lining") == nullptr, "playing transport with silent PCM retains visible waves");
  check(host.exec("=resume-signal", "testClock+=50; onAudioSpectrum(string.rep('0.6,',23)..'0.6')"),
        "audio did not resume");
  check(node("mina-lining") != nullptr, "waves do not return with real audio");
  check(host.exec("=stale", "testClock+=1000; update()"), "stale update failed");
  const auto settled = [&]() { return node("mina-lining") == nullptr; };
  check(settled(), "stale spectrum retains decorative waves");
  deliver(R"({"linked":true,"status":"paused","title":"Paused track"})");
  check(host.exec("=paused-signal", "testClock+=50; onAudioSpectrum(string.rep('1,',23)..'1')"), "paused signal rejected");
  check(settled(), "paused track renders waves from unrelated system audio");
  deliver(R"({"linked":true,"status":"stopped","title":"Stopped track"})");
  check(host.exec("=stopped-signal", "testClock+=50; onAudioSpectrum(string.rep('1,',23)..'1')"), "stopped signal rejected");
  check(settled(), "stopped track renders waves");
  check(node("mina-album") == nullptr, "missing album retained previous track's metadata");
  deliver("null");
  check(node("mina-cover") == nullptr, "missing player retained stale cover");
  check(
      node("mina-title") && std::get<std::string>(node("mina-title")->props.at("text")) == "No player",
      "missing player kept stale metadata"
  );
  deliver(R"({"linked":true,"status":"playing","title":"Malformed signal"})");
  check(host.exec("=bad-signal", "testClock+=50; onAudioSpectrum('1,nan')"), "malformed signal crashed");
  check(settled(), "malformed signal retains waves");
  deliver(R"({"linked":true,"status":"playing","title":"A very long title that must not expand the player","artist":"Artist","position":5,"duration":0})");
  check(host.exec("=small-output", "onBarGeometry(2456,180)"), "second monitor geometry rejected");
  check(node("mina-title") && std::get<double>(node("mina-title")->props.at("maxWidth")) <= 696,
        "long title has no bounded layout");
  check(node("mina-time") && std::get<std::string>(node("mina-time")->props.at("text")).ends_with("--:--"),
        "unknown duration is fabricated");
  check(node("mina-album") == nullptr, "absent album has a placeholder row");
  deliver(R"({"linked":false,"title":"Disconnected","artPath":"/tmp/stale.png","album":"Stale album"})");
  check(node("mina-cover") == nullptr && node("mina-album") == nullptr,
        "disconnected source leaks stale art or album");
  return failures == 0 ? 0 : 1;
}
