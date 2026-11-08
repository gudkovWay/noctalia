#include "config/config_service.h"
#include "render/core/color.h"
#include "render/core/image_file_loader.h"
#include "shell/tray/tray_drawer_panel.h"
#include "shell/tray/tray_fan_art.h"
#include "shell/tray/tray_fan_geometry.h"
#include "shell/tray/tray_fold_geometry.h"
#include "shell/tray/tray_settings.h"
#include "theme/builtin_palettes.h"
#include "ui/app_icon_colorization.h"
#include "util/base64.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <print>
#include <unistd.h>

int main() {
  int failures = 0;
  const auto check = [&](bool condition, const char* why) {
    if (!condition) {
      ++failures;
      std::println(stderr, "tray_fan: {}", why);
    }
  };
  const Color crimson = rgba(196.0F / 255.0F, 91.0F / 255.0F, 106.0F / 255.0F);
  std::array<std::uint8_t, 12> automaticInk{255, 255, 255, 255, 64, 64, 64, 255, 255, 255, 255, 0};
  auto preservedInk = automaticInk;
  bakeAppIconForColorization(automaticInk.data(), 3, 1, crimson);
  bakeAppIconForColorization(preservedInk.data(), 3, 1, crimson, true);
  check(preservedInk[0] > 150 && preservedInk[0] > automaticInk[0],
        "Mina red tint inverts bright icon details into unreadable dark ink");
  check(preservedInk[0] > preservedInk[4] && preservedInk[3] == 255 && preservedInk[11] == 0,
        "Mina icon tint loses internal contrast or transparent pixels");
  for (const float scale : {1.0F, 1.25F}) {
    for (const std::size_t count : {0U, 1U, 2U, 7U, 8U, 9U, 17U}) {
      const tray::FoldLayout first(count, 0, scale);
      const tray::FoldLayout last(count, 100, scale);
      check(last.page() + 1 == last.pages(), "fold overflow page does not clamp");
      check(first.width() == tray::kFoldWidth * scale, "fold width changes with app inventory");
      check(first.height() == last.height(), "paging moves the fold panel anchor");
      std::size_t reached = 0;
      for (std::size_t page = 0; page < first.pages(); ++page) {
        const tray::FoldLayout layout(count, page, scale);
        reached += layout.count();
        for (std::size_t slot = 0; slot < layout.count(); ++slot) {
          const auto p = layout.appPosition(slot);
          const float half = tray::kFoldTargetSize * scale * 0.5F;
          check(p.x - half >= 0 && p.x + half <= layout.width()
                    && p.y - half >= 0 && p.y + half <= layout.height(),
                "fold application hit target escapes its surface");
          for (const float dx : {-25.0F * scale, 0.0F, 25.0F * scale})
            for (const float dy : {-25.0F * scale, 0.0F, 25.0F * scale})
              check(layout.itemAt(p.x + dx, p.y + dy) == slot,
                    "fold logo target selects a neighbor");
        }
        check(!layout.itemAt(0, 0) && !layout.itemAt(layout.width() - 1, layout.height() - 1),
              "transparent fold corner activates an application");
      }
      check(reached == count, "fold pagination loses applications");
    }
  }
  for (const std::size_t count : {0U, 1U, 7U, 8U, 17U}) {
    const tray::FoldLayout first(count, 0, 1.0F, true);
    const tray::FoldLayout last(count, 100, 1.0F, true);
    check(first.width() == last.width() && first.height() == last.height(),
          "M10 tray resizes when inventory or page changes");
    std::size_t reached = 0;
    for (std::size_t page = 0; page < first.pages(); ++page) {
      const tray::FoldLayout layout(count, page, 1.0F, true);
      reached += layout.count();
      for (std::size_t slot = 0; slot < layout.count(); ++slot) {
        const auto point = layout.appPosition(slot);
        check(layout.itemAt(point.x, point.y) == slot && layout.itemAt(point.x + 160, point.y) == slot,
              "M10 tray row label does not activate its application");
        if (slot + 1 < layout.count())
          check(layout.appPosition(slot + 1).y - point.y >= 52.0F, "M10 tray row targets overlap");
      }
    }
    check(reached == count, "M10 tray pagination loses applications");
    check(!first.itemAt(0, 0) && !first.itemAt(first.width() - 1, first.height() - 1),
          "transparent M10 wing corner activates an application");
  }
  // Overflow must remain reachable; stale pages after a removal must clamp.
  tray::FanLayout eight(8, 0, tray::kFanExtent);
  check(
      eight.pages() == 1 && eight.first() == 0 && eight.count() == 8,
      "eight applications unnecessarily require a second page"
  );
  tray::FanLayout overflow(9, 0, tray::kFanExtent);
  check(overflow.pages() == 2 && overflow.count() == 8, "first page lost overflow items");
  tray::FanLayout last(9, 1, tray::kFanExtent);
  check(last.first() == 8 && last.count() == 1, "last application is unreachable");
  tray::FanLayout shrunk(2, 1, tray::kFanExtent);
  check(shrunk.page() == 0 && shrunk.first() == 0 && shrunk.count() == 2, "removal left an empty stale page");
  tray::FanLayout empty(0, 9, tray::kFanExtent);
  check(empty.count() == 0 && !empty.sectorAt(200.0F, 200.0F), "empty fan activates an application");
  // Real scene hit coordinates: transparent corners and hinge cannot activate apps.
  tray::FanLayout three(3, 0, 256.0F);
  check(
      !three.sectorAt(252, 252) && !three.sectorAt(244, 12) && !three.sectorAt(0, 0),
      "transparent/hinge area selects an application"
  );
  check(
      three.sectorAt(80, 60) == 0 && three.sectorAt(120, 136) == 1 && three.sectorAt(200, 180) == 2,
      "fan sector ordering/hit mapping is wrong"
  );
  // Even on a full page, a 44px target around each logo must not select its neighbor.
  for (const float scale : {1.0F, 1.5F}) {
    for (std::size_t count = 1; count <= 8; ++count) {
      tray::FanLayout layout(count, 0, tray::kFanExtent * scale);
      for (std::size_t slot = 0; slot < count; ++slot) {
        const auto point = layout.appPosition(slot);
        for (const float dx : {-22.0F * scale, 0.0F, 22.0F * scale}) {
          for (const float dy : {-22.0F * scale, 0.0F, 22.0F * scale}) {
            check(
                layout.sectorAt(point.x + dx, point.y + dy) == slot,
                "logo target activates a neighboring application or transparent paper"
            );
          }
        }
      }
    }
  }
  // Scaling preserves the same selection at a smaller compositor-configured size.
  tray::FanLayout small(3, 0, 128.0F);
  check(small.sectorAt(60, 68) == 1, "configured-size scaling broke hit targets");
  // Reversal starts at the current progress, never from a new closed/open frame.
  check(
      std::abs(tray::fanRevealDuration(0.4F, true) - 204.0F) < 0.01F
          && std::abs(tray::fanRevealDuration(0.4F, false) - 88.0F) < 0.01F,
      "interruption restarted a full-duration animation"
  );
  check(
      !tray::fanActionsEnabled(0.0F) && !tray::fanActionsEnabled(0.99F) && tray::fanActionsEnabled(1.0F),
      "moving paper accepts app activation"
  );
  check(
      tray::fanContentOpacity(0.7F) == 0 && tray::fanContentOpacity(1.0F) == 1,
      "logos appear on folded paper or disappear at rest"
  );
  for (std::size_t leaves = 1; leaves <= tray::kFanPageSize; ++leaves) {
    for (std::size_t leaf = 0; leaf <= leaves; ++leaf) {
      check(
          std::abs(tray::fanLeafAngle(leaf, 0, leaves) + tray::kFanSweep) < 0.0001F,
          "closed ribs do not stack at the hinge"
      );
    }
    check(
        std::abs(tray::fanLeafAngle(0, 1, leaves)) < 0.0001F
            && std::abs(tray::fanLeafAngle(leaves, 1, leaves) + tray::kFanSweep) < 0.0001F,
        "a partially filled page no longer unfolds across the full paper arc"
    );
  }

  // Exercise the native SVG loader, including alpha outside the actual paper.
  const auto paper = loadImageFile("data:image/svg+xml;base64," + Base64::encode(tray::fanPaperSvg(5)));
  check(paper.has_value(), "native loader cannot render fan paper");
  if (paper) {
    check(
        paper->rgba[(10 * paper->width + 100) * 4 + 3] > 0 && paper->rgba[(300 * paper->width + 300) * 4 + 3] == 0,
        "paper lost its silhouette/transparency"
    );
    check(
        paper->rgba[(70 * paper->width + 100) * 4 + 3] > 0,
        "application section paper does not cover its full hit sector"
    );
    // Texture relief must preserve the palette hue and readable light/dark pairs.
    for (const auto point : {tray::FanPoint{100, 10}, tray::FanPoint{100, 35}, tray::FanPoint{50, 45}}) {
      const auto offset = (static_cast<std::size_t>(point.y) * paper->width + static_cast<std::size_t>(point.x)) * 4U;
      const Color material =
          rgba(colorByte(paper->rgba[offset]), colorByte(paper->rgba[offset + 1]), colorByte(paper->rgba[offset + 2]));
      check(
          std::abs(material.r - material.g) <= 1.0F / 255.0F && std::abs(material.g - material.b) <= 1.0F / 255.0F,
          "paper texture injects a fixed hue into the active Noctalia palette"
      );
      for (const auto name : {"Noctalia", "Catppuccin", "Gruvbox"}) {
        const auto* scheme = noctalia::theme::findBuiltinPalette(name);
        check(scheme != nullptr, "native palette fixture is unavailable");
        if (!scheme)
          continue;
        for (const auto* mode : {&scheme->light, &scheme->dark}) {
          for (const bool hovered : {false, true}) {
            const Color surface =
                hovered ? lerpColor(mode->palette.surface, mode->palette.primary, 0.12F) : mode->palette.surface;
            const Color background = rgba(material.r * surface.r, material.g * surface.g, material.b * surface.b);
            const float backgroundLuminance = relativeLuminance(background);
            const float inkLuminance = relativeLuminance(mode->palette.onSurface);
            check(
                (std::max(backgroundLuminance, inkLuminance) + 0.05F)
                        / (std::min(backgroundLuminance, inkLuminance) + 0.05F)
                    >= 4.5F,
                "paper relief or hover hides icons on a native light/dark palette"
            );
          }
        }
      }
    }
  }
  const auto ribs = loadImageFile("data:image/svg+xml;base64," + Base64::encode(tray::kFanRibsSvg));
  check(ribs.has_value(), "native loader cannot render fan ribs");
  if (ribs) {
    check(
        ribs->rgba[(1 * ribs->width + 100) * 4 + 3] > 0 && ribs->rgba[(35 * ribs->width + 100) * 4 + 3] == 0,
        "rib material is missing or covers the icon-bearing paper"
    );
    check(ribs->rgba[(63 * ribs->width + 5) * 4 + 3] == 0, "leaf ribs still create a segmented outer contour");
    for (std::size_t offset = 0; offset < ribs->rgba.size(); offset += 4) {
      if (ribs->rgba[offset + 3] == 0)
        continue;
      check(
          ribs->rgba[offset] == ribs->rgba[offset + 1] && ribs->rgba[offset + 1] == ribs->rgba[offset + 2],
          "rib texture injects a fixed hue into the active Noctalia palette"
      );
    }
  }
  const auto outline = loadImageFile("data:image/svg+xml;base64," + Base64::encode(tray::kFanOutlineSvg));
  check(outline.has_value(), "native loader cannot render the continuous outer edge");
  if (outline) {
    for (int degrees = 0; degrees <= 90; ++degrees) {
      const float angle = static_cast<float>(degrees) * std::numbers::pi_v<float> / 180.0F;
      const auto x = static_cast<std::size_t>(408.0F - 400.0F * std::cos(angle));
      const auto y = static_cast<std::size_t>(8.0F + 400.0F * std::sin(angle));
      check(
          outline->rgba[(y * outline->width + x) * 4 + 3] > 0, "continuous outline has a gap along the circular edge"
      );
    }
    check(
        outline->rgba[(8 * outline->width + 7) * 4 + 3] > 0 && outline->rgba[(408 * outline->width + 409) * 4 + 3] > 0,
        "round outline caps are clipped by the image bounds"
    );
    check(
        outline->rgba[(100 * outline->width + 200) * 4 + 3] == 0
            && outline->rgba[(415 * outline->width + 415) * 4 + 3] == 0,
        "outline paints over the paper or transparent corners"
    );
  }
  for (std::size_t leaves = 1; leaves <= tray::kFanPageSize; ++leaves) {
    const auto section = loadImageFile("data:image/svg+xml;base64," + Base64::encode(tray::fanPaperSvg(leaves)));
    check(section.has_value(), "native loader cannot render a partially filled page");
    if (section) {
      const float angle = tray::kFanSweep / (2.0F * static_cast<float>(leaves));
      const auto x = static_cast<std::size_t>(400.0F - 360.0F * std::cos(angle));
      const auto y = static_cast<std::size_t>(360.0F * std::sin(angle));
      check(
          section->rgba[(y * section->width + x) * 4 + 3] > 0 && section->rgba[(399 * section->width + 1) * 4 + 3] == 0,
          "partial-page logos fall outside their paper or paper fills transparent corners"
      );
    }
  }
  const auto root = std::filesystem::temp_directory_path() / ("noctalia-fan-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "config/noctalia");
  std::ofstream(root / "config/noctalia/config.toml") << R"(
[bar.skin-runtime-status]
position = 'top'
end = ['q_skin_runtime_tray']
[widget.tray]
hidden = ['canonical-app']
[widget.q_skin_runtime_tray]
type = 'tray'
hidden = ['instance-app']
[bar.mina-left]
position = 'left'
center = ['mina_tray']
[widget.mina_tray]
type = 'tray'
drawer_background_image = 'fold.png'
icon_color = '#C45B6A'
)";
  ::setenv("NOCTALIA_CONFIG_HOME", (root / "config").c_str(), 1);
  ::setenv("NOCTALIA_STATE_HOME", (root / "state").c_str(), 1);
  {
    ConfigService config;
    const auto own = tray::resolvedTrayOptions(config, {}, "q_skin_runtime_tray");
    const auto canonical = tray::resolvedTrayOptions(config);
    const auto mina = tray::resolvedTrayOptions(config, {}, "mina_tray");
    check(mina.iconColor == colorSpecFromConfigString("#C45B6A"), "material drawer lost its configured icon color");
    check(!canonical.iconColor && !own.iconColor, "Mina icon color leaked into another tray instance");
    check(
        own.options.hiddenItems == std::vector<std::string>{"instance-app"}
            && canonical.options.hiddenItems == std::vector<std::string>{"canonical-app"},
        "opening the named tray borrows/changes canonical filters"
    );
    TrayDrawerPanel panel(nullptr, &config);
    panel.setPendingOpenContext("q_skin_runtime_tray");
    check(
        panel.fanPresentation() && !panel.hasDecoration() && panel.panelPlacement() == PanelPlacement::Floating,
        "owned opener does not open an undecorated fan"
    );
    panel.onOpen("q_skin_runtime_tray");
    check(panel.isContextActive("q_skin_runtime_tray"), "second click cannot close the same tray");
    panel.onClose();
    panel.setPendingOpenContext("tray");
    check(!panel.fanPresentation() && panel.hasDecoration(), "fan styling leaked into canonical tray");
    panel.setPendingOpenContext("mina_tray");
    check(!panel.fanPresentation() && !panel.hasDecoration() && panel.panelPlacement() == PanelPlacement::Attached,
          "material drawer gained a standard rectangular decoration or fan placement");
    check(panel.preferredWidth() == tray::FoldLayout(0, 0).width()
              && panel.preferredHeight() == tray::FoldLayout(0, 0).height(),
          "material drawer geometry disagrees with its native hit targets");
  }
  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
