#include "shell/skins/skin_service.h"

#include "config/config_service.h"
#include "config/config_types.h"
#include "config/schema/config_schema.h"
#include "core/files/resource_paths.h"
#include "core/log.h"
#include "core/toml.h"
#include "launcher/panel_catalog.h"
#include "scripting/plugin_manifest.h"
#include "scripting/plugin_registry.h"
#include "scripting/plugin_source_paths.h"
#include "scripting/plugin_state_store.h"
#include "shell/panel/panel_manager.h"
#include "shell/settings/widget_settings_registry.h"
#include "ui/palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
  constexpr Logger kLog("skin");

  // The only plugin whose settings this service owns, and the only panel it opens or
  // closes. Everything else in the shell belongs to its own owner.
  constexpr std::string_view kPluginId = "q/skin-runtime";
  constexpr std::string_view kMusicPanelId = "q/skin-runtime:music";
  constexpr std::string_view kStatusPanelId = "q/skin-runtime:desktop-status";
  constexpr std::string_view kPathRoot = "plugin_settings";
  // StateStore owners/keys are restricted to [a-z0-9_]; anything else is rejected.
  constexpr std::string_view kStateOwner = "skin_runtime";
  constexpr std::string_view kReceiptKey = "active";
  // Name of the path source that makes the packaged presentation plugin discoverable.
  // Only ever appended when nothing already covers the packaged root.
  constexpr std::string_view kPresentationSourceName = "skin-runtime-assets";

  // The one bar this service owns. The name is fixed and reserved: a bar the user
  // already configured under it is never adopted, and a receipt that names any other bar
  // is rejected. The widget id is the bar entry the whole projection exists to host.
  constexpr std::string_view kStatusBarName = "skin-runtime-status";
  constexpr std::string_view kStatusBarWidgetId = "q/skin-runtime:skin-widget";
  constexpr std::string_view kMusicBarWidgetId = "q/skin-runtime:music-preview";
  constexpr std::string_view kStatusBarClockId = "clock";
  // Dedicated native tray instance the owned Kumihimo bar hosts at its end lane. The id
  // is a named `[widget.<id>]` table of the built-in `tray` type — never the global
  // `widget.tray` entry, which stays the user's. The name uses only the characters the
  // settings UI itself generates for instances (alnum + underscore), and it is reserved:
  // a table the user already configured under it is never adopted. SNI actions and the
  // tray's own behavior are untouched; only this instance's capsule housing is styled.
  constexpr std::string_view kTrayInstanceId = "q_skin_runtime_tray";
  constexpr std::string_view kTrayWidgetType = "tray";
  // Compact recipe-colored housing for the dedicated tray instance: a small explicit
  // capsule, not the bar's auto-radius pill.
  constexpr std::int64_t kTrayCapsulePadding = 4;
  constexpr double kTrayCapsuleRadius = 6.0;
  constexpr double kTrayCapsuleBorderWidth = 1.0;
  // Owned Kumihimo bar geometry, frozen per receipt revision. The compact strip reserves
  // a 64px top zone (widget root 56 + bar padding 4x2) tall enough for the plugin's
  // hanging tag row and wide enough that its 56px slot cross gives the dedicated tray
  // capsule slotCross * 0.625 = 35px. The pre-compact strip is the shape every receipt
  // written before the compact repair actually recorded: 120px tall, padding 8, no
  // exclusive zone and no `layer` field. Only Kumihimo installs this bar; Stagecraft owns
  // no bar.
  struct KumihimoBarGeometry {
    std::int64_t thickness;
    std::int64_t padding;
    bool reserveSpace;
    double capsuleThickness;
    bool writesLayer;
  };
  constexpr KumihimoBarGeometry kKumihimoBarGeometryCompact{64, 4, true, 0.625, true};
  constexpr KumihimoBarGeometry kKumihimoBarGeometryPreCompact{120, 8, false, 0.3125, false};
  // Frozen receipt shapes: 0 pre-compact, 1 compact tags, 2 centered music.
  // Missing or unsupported revisions retain the historical pre-compact shape.
  constexpr std::int32_t kKumihimoBarGeometryRevisionOld = 0;
  constexpr std::int32_t kKumihimoBarGeometryRevisionCompact = 1;
  constexpr std::int32_t kKumihimoBarGeometryRevisionMusic = 2;
  constexpr std::int32_t kKumihimoBarGeometryRevisionJoinery = 3;
  constexpr std::int32_t kKumihimoBarGeometryRevisionAiCassette = 4;
  // Name of the bar the shell synthesizes when the config defines none; it must be
  // materialized as an override so creating ours does not delete it.
  constexpr std::string_view kImplicitDefaultBar = "default";

  // Existing data owners read for the curated status payload. Never written to.
  constexpr std::string_view kTmusPluginId = "q/tmus";
  constexpr std::string_view kAiTriagePluginId = "q/ai-triage";
  // Adapted presentation surfaces, claimed in the surface catalog only while a
  // Stagecraft profile is active.
  constexpr std::string_view kWallpaperStudioPluginId = "q/wallpaper-studio";

  constexpr std::size_t kMaxNameBytes = 100;
  constexpr std::size_t kMaxPreviewPathBytes = 4096;
  constexpr std::size_t kMaxQueueEntries = 2;

  // Enabled families. Stagecraft keeps its three topologies and its global chrome;
  // Kumihimo is its own wallpaper-driven family: the hanging-tags topology and no global
  // chrome or passive Stagecraft panels. The remaining catalog directions stay absent
  // from the supported list until their own wave.
  constexpr std::array<std::string_view, 2> kSupportedFamilies{"stagecraft", "kumihimo"};
  constexpr std::array<std::string_view, 5> kKnownFamilies{
      "stagecraft", "cut-stone", "wayfinding-field", "adaptive-workbench", "kumihimo"
  };
  constexpr std::array<std::string_view, 3> kStagecraftTopologies{
      "top-music-side", "top-music-bottom", "desktop-widget-side"
  };
  constexpr std::array<std::string_view, 1> kKumihimoTopologies{"hanging-tags"};
  constexpr std::array<std::string_view, 5> kTopologies{
      "top-music-side", "top-music-bottom", "desktop-widget-side", "compact-rail", "hanging-tags"
  };
  constexpr std::array<std::string_view, 2> kDensities{"compact", "comfortable"};
  constexpr std::array<std::string_view, 6> kColorKeys{"surface", "surface_variant", "text",
                                                       "muted",   "accent",          "outline"};
  constexpr std::array<std::string_view, 10> kRecipeKeys{"schema_version", "target",      "variant_id", "content_id",
                                                         "name",           "family",      "topology",   "colors",
                                                         "density",        "preview_path"};

  using SettingList = std::vector<std::pair<std::string, std::string>>;

  [[nodiscard]] bool isLowerHex(std::string_view text, std::size_t length) {
    if (text.size() != length) {
      return false;
    }
    for (const char character : text) {
      if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool isColorHex(std::string_view text) {
    if (text.size() != 7 || text.front() != '#') {
      return false;
    }
    for (const char character : text.substr(1)) {
      const bool digit = character >= '0' && character <= '9';
      const bool lower = character >= 'a' && character <= 'f';
      const bool upper = character >= 'A' && character <= 'F';
      if (!digit && !lower && !upper) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] std::string toLowerAscii(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
      if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character - 'A' + 'a');
      }
    }
    return out;
  }

  [[nodiscard]] bool hasControlChar(std::string_view text) {
    for (const char character : text) {
      const auto byte = static_cast<unsigned char>(character);
      if (byte < 0x20 || byte == 0x7f) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::vector<std::string> overridePath(std::string_view key) {
    return {std::string(kPathRoot), std::string(kPluginId), std::string(key)};
  }

  // Screen position for the persistent music panel, derived from the recipe topology.
  // "top" topologies pin the panel to the top edge; the desktop-widget family keeps the
  // music stage out of the working area's way in the bottom corner. Placement stays
  // floating (persistent panels always are), so only the position is published.
  [[nodiscard]] std::optional<std::string> musicPositionFor(const noctalia::shell::skins::SkinRecipe& recipe) {
    if (recipe.family != "stagecraft") {
      return std::nullopt;
    }
    if (recipe.topology == "top-music-side" || recipe.topology == "top-music-bottom") {
      return std::string("top_center");
    }
    if (recipe.topology == "desktop-widget-side") {
      return std::string("bottom_right");
    }
    return std::nullopt;
  }

  // Screen position for the owned persistent desktop status panel, derived from the same
  // topology. The concept keeps a narrow system panel against the LEFT edge for the
  // top-music-side and desktop-widget-side topologies and moves it to the bottom edge for
  // the bottom-music topology. Only existing position vocabulary is published.
  [[nodiscard]] std::optional<std::string> statusPositionFor(const noctalia::shell::skins::SkinRecipe& recipe) {
    if (recipe.family != "stagecraft") {
      return std::nullopt;
    }
    if (recipe.topology == "top-music-bottom") {
      return std::string("bottom_center");
    }
    if (recipe.topology == "top-music-side" || recipe.topology == "desktop-widget-side") {
      return std::string("center_left");
    }
    return std::nullopt;
  }

  // The bounded set of plugin settings a recipe is published into, using the exact keys
  // the q/skin-runtime manifest declares. Adding a key here is the only way a recipe can
  // touch the config. `applied_variant` is the cross-plugin identity marker: the applied
  // recipe's variant id while active, manifest-default empty after a clear — foreign
  // surfaces (the Studio picker, adapted plugin presentations) gate on a 32-hex value
  // here instead of the family value, whose default is also "stagecraft".
  [[nodiscard]] std::vector<std::pair<std::string, std::string>>
  projectionOf(const noctalia::shell::skins::SkinRecipe& recipe) {
    std::vector<std::pair<std::string, std::string>> settings{
        {"applied_variant", recipe.variantId},
        {"family", recipe.family},
        {"topology", recipe.topology},
        {"density", recipe.density},
        {"surface", recipe.colorSurface},
        {"surface_variant", recipe.colorSurfaceVariant},
        {"text", recipe.colorText},
        {"muted", recipe.colorMuted},
        {"accent", recipe.colorAccent},
        {"outline", recipe.colorOutline},
    };
    // Host-side panel placement the topology selects; absent for families whose
    // presentation does not own this panel.
    if (const auto position = musicPositionFor(recipe); position.has_value()) {
      settings.emplace_back("music_position", *position);
    }
    if (const auto position = statusPositionFor(recipe); position.has_value()) {
      settings.emplace_back("desktop-status_position", *position);
    }
    return settings;
  }

  // One fixed field of the owned status bar: the schema key plus the typed value the
  // recipe projects into it. The field set is closed — no recipe key can add a path.
  struct BarFieldSpec {
    std::string field;
    ConfigOverrideValue value;
  };

  [[nodiscard]] std::vector<std::string> barOverridePath(std::string_view field) {
    return {"bar", std::string(kStatusBarName), std::string(field)};
  }

  [[nodiscard]] const BarConfig* findBar(const Config& config, std::string_view name) {
    const auto it = std::ranges::find(config.bars, name, &BarConfig::name);
    return it != config.bars.end() ? &*it : nullptr;
  }

  // The bounded status-bar projection a legacy receipt installs. Field names are exactly
  // the `[bar.<name>]` schema keys; every value is a literal or a recipe field, so the
  // recipe — never the model or a receipt — decides what is written. Stagecraft itself no
  // longer creates a bar; this shape survives only so an old receipt's generated
  // `skin-runtime-status` bar can still be compared and retired.
  [[nodiscard]] std::vector<BarFieldSpec> stagecraftBarProjectionOf(const noctalia::shell::skins::SkinRecipe& recipe) {
    const bool compact = recipe.density == "compact";
    // The bar is a compact normal overview launcher pinned to the top edge, or the
    // bottom edge for the bottom-music topology. It is never the side scene: the
    // desktop system panel is the owned persistent q/skin-runtime:desktop-status, so
    // the bar stays a plain horizontal host for the overview widget and the clock.
    const std::string position = recipe.topology == "top-music-bottom" ? "bottom" : "top";
    return {
        {"enabled", true},
        {"position", position},
        {"width_mode", std::string("content")},
        {"thickness", static_cast<std::int64_t>(compact ? 40 : 48)},
        {"margin_ends", static_cast<std::int64_t>(24)},
        {"margin_edge", static_cast<std::int64_t>(12)},
        {"padding", static_cast<std::int64_t>(compact ? 6 : 8)},
        {"widget_spacing", static_cast<std::int64_t>(8)},
        {"radius", static_cast<std::int64_t>(12)},
        {"concave_edge_corners", false},
        {"reserve_space", false},
        {"auto_hide", false},
        {"background_opacity", 0.0},
        {"shadow", false},
        {"capsule", true},
        {"capsule_fill", recipe.colorSurfaceVariant},
        {"capsule_foreground", recipe.colorText},
        {"color", recipe.colorText},
        {"icon_color", recipe.colorText},
        {"capsule_border", recipe.colorOutline},
        {"capsule_border_width", 1.0},
        {"start", std::vector<std::string>{}},
        {"center", std::vector<std::string>{std::string(kStatusBarWidgetId), std::string(kStatusBarClockId)}},
        {"end", std::vector<std::string>{}},
    };
  }

  // One owned Kumihimo bar shape: a full-width, transparent top strip tall enough to hang
  // the plugin's tag row from. The widget at the start lane renders the braid and the
  // distinct Music/AI/clock/Control tags; the dedicated native tray instance sits at the
  // end lane untouched. The bar owns no capsule fill and draws no plate of its own, so the
  // only visible geometry is the widget's — no second ornamental frame. hover_highlight=
  // false is written ONLY for this owned bar: the user's own bars keep their configured
  // hover pill. `layer` is written only from the compact repair on; the older shapes omit
  // it so a receipt's recorded field set stays exactly what its version wrote.
  [[nodiscard]] std::vector<BarFieldSpec>
  kumihimoBarProjectionOf(const KumihimoBarGeometry& geometry, bool centeredMusic = false, bool aiCassette = false) {
    std::vector<BarFieldSpec> projection{
        {"enabled", true},
        {"position", std::string("top")},
        {"width_mode", std::string("available")},
        {"thickness", geometry.thickness},
        {"margin_ends", static_cast<std::int64_t>(0)},
        {"margin_edge", static_cast<std::int64_t>(0)},
        {"padding", geometry.padding},
        {"widget_spacing", static_cast<std::int64_t>(12)},
        {"radius", static_cast<std::int64_t>(0)},
        {"concave_edge_corners", false},
        {"reserve_space", geometry.reserveSpace},
        {"auto_hide", false},
        {"background_opacity", 0.0},
        {"shadow", false},
        {"capsule", false},
        // Bounds the native tray capsule cross (bar.cpp computes
        // capsuleCross = slotCross * capsuleThickness); owned bar only, never legacy.
        {"capsule_thickness", geometry.capsuleThickness},
        {"hover_highlight", false},
        {"start",
         aiCassette
             ? std::vector<
                   std::
                       string>{std::string(kStatusBarWidgetId), "q/ai-triage:ai-triage-widget", "q/skin-runtime:bar-clock"}
             : std::vector<std::string>{std::string(kStatusBarWidgetId)}},
        {"center",
         centeredMusic ? std::vector<std::string>{std::string(kMusicBarWidgetId)} : std::vector<std::string>{}},
        {"end", std::vector<std::string>{std::string(kTrayInstanceId)}},
    };
    if (geometry.writesLayer) {
      projection.push_back(BarFieldSpec{"layer", std::string("top")});
    }
    return projection;
  }

  // The current owned Kumihimo bar: the compact reserved top strip.
  [[nodiscard]] std::vector<BarFieldSpec> kumihimoBarProjectionOf() {
    return kumihimoBarProjectionOf(kKumihimoBarGeometryCompact, true, true);
  }

  // The shape receipts written before the compact repair actually recorded: the same
  // fields with the pre-compact geometry and no `layer`.
  [[nodiscard]] std::vector<BarFieldSpec> kumihimoPreCompactBarProjectionOf() {
    return kumihimoBarProjectionOf(kKumihimoBarGeometryPreCompact);
  }

  // The Kumihimo bar shape as receipts written BEFORE the dedicated tray instance, the
  // owned hover suppression and the owned capsule_thickness recorded it: the bare
  // built-in `tray` at the end lane and neither a `hover_highlight` nor a
  // `capsule_thickness` field. Used only to recognize what such a receipt actually
  // wrote — for reapply migration, activation and clear — never for a fresh install.
  [[nodiscard]] std::vector<BarFieldSpec> kumihimoLegacyBarProjectionOf() {
    std::vector<BarFieldSpec> legacy = kumihimoPreCompactBarProjectionOf();
    // Drop the fields the old shape did not write.
    std::erase_if(legacy, [](const BarFieldSpec& spec) {
      return spec.field == "hover_highlight" || spec.field == "capsule_thickness";
    });
    for (auto& spec : legacy) {
      if (spec.field == "end") {
        spec.value = ConfigOverrideValue{std::vector<std::string>{std::string("tray")}};
      }
    }
    return legacy;
  }

  // Whether a receipt predates the dedicated tray instance: a Kumihimo receipt whose
  // recorded bar originals contain no `hover_highlight` entry. The receipt's own recorded
  // field set — not the current static projection — is the bounded discriminator.
  [[nodiscard]] bool isLegacyKumihimoReceipt(const noctalia::shell::skins::SkinReceipt& receipt) {
    return receipt.barOwned
        && receipt.barName == kStatusBarName
        && receipt.recipe.family == "kumihimo"
        && std::ranges::none_of(receipt.barOriginals, [](const noctalia::shell::skins::OwnedSetting& original) {
             return original.key == "hover_highlight";
           });
  }

  // Bar shape of a recipe: Kumihimo owns a real bar, every other family (and every
  // Stagecraft-era receipt) uses the retired legacy shape only for comparison/retirement.
  [[nodiscard]] std::vector<BarFieldSpec> barProjectionOf(const noctalia::shell::skins::SkinRecipe& recipe) {
    if (recipe.family == "kumihimo") {
      return kumihimoBarProjectionOf();
    }
    return stagecraftBarProjectionOf(recipe);
  }

  // The bar shape a receipt actually wrote: the pre-correction shape for receipts that
  // predate the dedicated tray instance, the pre-compact shape for an owned Kumihimo
  // receipt whose bounded geometry revision is still 0, the current static projection
  // otherwise. Never used to install a fresh bar — only to compare, migrate, activate and
  // clear what a given receipt wrote.
  [[nodiscard]] std::vector<BarFieldSpec> receiptBarProjectionOf(const noctalia::shell::skins::SkinReceipt& receipt) {
    if (isLegacyKumihimoReceipt(receipt)) {
      return kumihimoLegacyBarProjectionOf();
    }
    if (receipt.barOwned
        && receipt.barName == kStatusBarName
        && receipt.recipe.family == "kumihimo"
        && receipt.barGeometryRevision == kKumihimoBarGeometryRevisionOld) {
      return kumihimoPreCompactBarProjectionOf();
    }
    if (receipt.barOwned
        && receipt.barName == kStatusBarName
        && receipt.recipe.family == "kumihimo"
        && receipt.barGeometryRevision == kKumihimoBarGeometryRevisionCompact) {
      return kumihimoBarProjectionOf(kKumihimoBarGeometryCompact);
    }
    if (receipt.barOwned
        && receipt.barName == kStatusBarName
        && receipt.recipe.family == "kumihimo"
        && (receipt.barGeometryRevision == kKumihimoBarGeometryRevisionMusic
            || receipt.barGeometryRevision == kKumihimoBarGeometryRevisionJoinery)) {
      return kumihimoBarProjectionOf(kKumihimoBarGeometryCompact, true);
    }
    return barProjectionOf(receipt.recipe);
  }

  // The value a projected bar field currently resolves to, in the same typed shape the
  // projection writes. nullopt means the field carries no value (an unset optional), so
  // it can never compare equal to a projected color or list.
  [[nodiscard]] std::optional<ConfigOverrideValue>
  effectiveBarFieldValue(const BarConfig& bar, std::string_view field) {
    if (field == "enabled")
      return ConfigOverrideValue{bar.enabled};
    if (field == "position")
      return ConfigOverrideValue{bar.position};
    if (field == "width_mode")
      return ConfigOverrideValue{bar.widthMode};
    if (field == "thickness")
      return ConfigOverrideValue{static_cast<std::int64_t>(bar.thickness)};
    if (field == "layer")
      return ConfigOverrideValue{bar.layer};
    if (field == "margin_ends")
      return ConfigOverrideValue{static_cast<std::int64_t>(bar.marginEnds)};
    if (field == "margin_edge")
      return ConfigOverrideValue{static_cast<std::int64_t>(bar.marginEdge)};
    if (field == "padding")
      return ConfigOverrideValue{static_cast<std::int64_t>(bar.padding)};
    if (field == "widget_spacing")
      return ConfigOverrideValue{static_cast<std::int64_t>(bar.widgetSpacing)};
    if (field == "radius")
      return ConfigOverrideValue{static_cast<std::int64_t>(bar.radius)};
    if (field == "concave_edge_corners")
      return ConfigOverrideValue{bar.concaveEdgeCorners};
    if (field == "reserve_space")
      return ConfigOverrideValue{bar.reserveSpace};
    if (field == "auto_hide")
      return ConfigOverrideValue{bar.autoHide};
    if (field == "background_opacity")
      return ConfigOverrideValue{static_cast<double>(bar.backgroundOpacity)};
    if (field == "shadow")
      return ConfigOverrideValue{bar.shadow};
    if (field == "capsule")
      return ConfigOverrideValue{bar.widgetCapsuleDefault};
    if (field == "hover_highlight")
      return ConfigOverrideValue{bar.hoverHighlight};
    if (field == "capsule_thickness")
      return ConfigOverrideValue{static_cast<double>(bar.capsuleThickness)};
    if (field == "capsule_fill")
      return ConfigOverrideValue{colorSpecToConfigString(bar.widgetCapsuleFill)};
    if (field == "capsule_foreground") {
      return bar.widgetCapsuleForeground.has_value()
          ? std::optional<ConfigOverrideValue>{colorSpecToConfigString(*bar.widgetCapsuleForeground)}
          : std::nullopt;
    }
    if (field == "color") {
      return bar.widgetColor.has_value() ? std::optional<ConfigOverrideValue>{colorSpecToConfigString(*bar.widgetColor)}
                                         : std::nullopt;
    }
    if (field == "icon_color") {
      return bar.widgetIconColor.has_value()
          ? std::optional<ConfigOverrideValue>{colorSpecToConfigString(*bar.widgetIconColor)}
          : std::nullopt;
    }
    if (field == "capsule_border") {
      if (!bar.widgetCapsuleBorderSpecified) {
        return std::nullopt;
      }
      return ConfigOverrideValue{
          bar.widgetCapsuleBorder.has_value() ? colorSpecToConfigString(*bar.widgetCapsuleBorder) : std::string{}
      };
    }
    if (field == "capsule_border_width")
      return ConfigOverrideValue{static_cast<double>(bar.widgetCapsuleBorderWidth)};
    if (field == "start")
      return ConfigOverrideValue{bar.startWidgets};
    if (field == "center")
      return ConfigOverrideValue{bar.centerWidgets};
    if (field == "end")
      return ConfigOverrideValue{bar.endWidgets};
    return std::nullopt;
  }

  // Color specs round-trip through ColorSpec as upper-case hex while recipes carry
  // lower-case, so scalar strings compare case-insensitively; everything else compares
  // by value.
  [[nodiscard]] bool overrideValueEquals(const ConfigOverrideValue& a, const ConfigOverrideValue& b) {
    const auto* aText = std::get_if<std::string>(&a);
    const auto* bText = std::get_if<std::string>(&b);
    if (aText != nullptr || bText != nullptr) {
      return aText != nullptr && bText != nullptr && toLowerAscii(*aText) == toLowerAscii(*bText);
    }
    return a == b;
  }

  // Receipt decoding for the typed bar originals left by a legacy receipt. Only the
  // bounded projection types are supported; anything else fails to restore and the field
  // is left alone.
  [[nodiscard]] std::optional<ConfigOverrideValue> decodeOverrideValue(const nlohmann::json& node) {
    if (node.is_boolean())
      return ConfigOverrideValue{node.get<bool>()};
    if (node.is_number_integer())
      return ConfigOverrideValue{node.get<std::int64_t>()};
    if (node.is_number_float())
      return ConfigOverrideValue{node.get<double>()};
    if (node.is_string())
      return ConfigOverrideValue{node.get<std::string>()};
    if (node.is_array()) {
      std::vector<std::string> values;
      values.reserve(node.size());
      for (const auto& item : node) {
        if (!item.is_string()) {
          return std::nullopt;
        }
        values.push_back(item.get<std::string>());
      }
      return ConfigOverrideValue{std::move(values)};
    }
    return std::nullopt;
  }

  // Whether a bar carries anything this skin did not write: a monitor override, or an
  // override for a known bar field outside `owned` (a settings-UI edit). The owned bars
  // are override-only, so any such key belongs to the user and blocks removal.
  [[nodiscard]] bool barHasForeignContent(
      const ConfigService& config, std::string_view barName, const std::unordered_set<std::string>& owned
  ) {
    const std::string name(barName);
    if (config.hasOverride({"bar", name, "monitor"})) {
      return true;
    }
    if (!owned.contains("position") && config.hasOverride({"bar", name, "position"})) {
      return true;
    }
    for (const auto& field : noctalia::config::schema::barFieldsSchema()) {
      const std::string key(field.key);
      if (owned.contains(key)) {
        continue;
      }
      if (config.hasOverride({"bar", name, key})) {
        return true;
      }
    }
    return false;
  }

  // Whether any bar lane still references a q/skin-runtime entry. A bar the user kept
  // after an edit needs the plugin registered, so a clear must not revert it.
  [[nodiscard]] bool presentationWidgetReferenced(const Config& config) {
    const std::string prefix = std::string(kPluginId) + ":";
    for (const auto& bar : config.bars) {
      for (const auto* lane : {&bar.startWidgets, &bar.centerWidgets, &bar.endWidgets}) {
        for (const auto& widget : *lane) {
          if (widget.starts_with(prefix)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  // One atomic override commit shared by apply, clear and rollback: values to write plus
  // paths to drop. Returns false and fills `error` when ConfigService refused it.
  [[nodiscard]] bool commitOverrides(
      ConfigService& config, const std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& sets,
      const std::vector<std::vector<std::string>>& clearPaths, std::string* error
  ) {
    if (sets.empty() && clearPaths.empty()) {
      return true;
    }
    bool changed = false;
    if (!config.mutateOverrides(sets, clearPaths, &changed)) {
      const std::string& detail = config.lastMutationError();
      if (error != nullptr) {
        *error = detail.empty() ? "failed to write the skin config overrides" : detail;
      }
      return false;
    }
    return true;
  }

  // One owned plugin setting by key, or nullptr when the receipt does not record it.
  [[nodiscard]] const noctalia::shell::skins::OwnedSetting*
  findOwned(const std::vector<noctalia::shell::skins::OwnedSetting>& originals, std::string_view key) {
    for (const auto& original : originals) {
      if (original.key == key) {
        return &original;
      }
    }
    return nullptr;
  }

  // Typed bar value serialized into the receipt's JSON shape, mirroring
  // decodeOverrideValue. Only the five bounded projection types are produced.
  [[nodiscard]] nlohmann::json overrideValueJson(const ConfigOverrideValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
      return nlohmann::json(*boolean);
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
      return nlohmann::json(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
      return nlohmann::json(*number);
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
      return nlohmann::json(*text);
    }
    if (const auto* list = std::get_if<std::vector<std::string>>(&value)) {
      return nlohmann::json(*list);
    }
    return nlohmann::json(nullptr);
  }

  // Legacy generated-bar cleanup for a receipt from a version that created
  // `bar.skin-runtime-status`. Collects, into `sets`/`clears`, the overrides that retire
  // that generated bar: drop it whole (plus the implicit-default carrier it may carry)
  // only while every field the receipt proves this skin wrote still matches and the bar
  // carries nothing else; otherwise restore only the still-owned fields and leave the
  // user-edited bar — and its widget — in place. `dropCarrier` reports whether the
  // carrier must go with the bar in the same commit. A receipt that does not own the
  // reserved bar collects nothing.
  void collectLegacyBarCleanup(
      const ConfigService& config, const noctalia::shell::skins::SkinReceipt& receipt,
      std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& sets,
      std::vector<std::vector<std::string>>& clears, bool& dropCarrier
  ) {
    if (!receipt.barOwned || receipt.barName != kStatusBarName) {
      return;
    }
    const BarConfig* bar = findBar(config.config(), kStatusBarName);
    if (bar == nullptr) {
      // The bar is already gone; only the carrier the skin added can remain.
      dropCarrier = receipt.barCarriedDefault;
      return;
    }
    const std::vector<BarFieldSpec> barProjection = receiptBarProjectionOf(receipt);
    std::unordered_set<std::string> ownedFields;
    bool allMatch = true;
    const bool hasUserOriginals =
        std::ranges::any_of(receipt.barOriginals, [](const noctalia::shell::skins::OwnedSetting& field) {
          return field.hadOverride;
        });
    for (const auto& spec : barProjection) {
      ownedFields.insert(spec.field);
      const auto current = effectiveBarFieldValue(*bar, spec.field);
      if (!current.has_value() || !overrideValueEquals(*current, spec.value)) {
        allMatch = false;
      }
    }
    if (allMatch && !hasUserOriginals && !barHasForeignContent(config, kStatusBarName, ownedFields)) {
      clears.push_back({"bar", std::string(kStatusBarName)});
      dropCarrier = receipt.barCarriedDefault;
      return;
    }
    for (const auto& spec : barProjection) {
      const auto current = effectiveBarFieldValue(*bar, spec.field);
      if (!current.has_value() || !overrideValueEquals(*current, spec.value)) {
        continue;
      }
      const noctalia::shell::skins::OwnedSetting* original = findOwned(receipt.barOriginals, spec.field);
      if (original != nullptr && original->hadOverride) {
        const auto decoded = decodeOverrideValue(nlohmann::json::parse(original->originalValue, nullptr, false));
        if (decoded.has_value()) {
          sets.emplace_back(barOverridePath(spec.field), *decoded);
        }
      } else if (config.hasOverride(barOverridePath(spec.field))) {
        clears.push_back(barOverridePath(spec.field));
      }
    }
  }

  // Drops the implicit-default carrier the skin materialized alongside a generated bar.
  // The legacy write was enabled=true (the shell's implicit default), so the carrier is
  // retired only while its CURRENT enabled value still equals that write: a user who
  // disabled the default bar — through the override or any other layer — keeps it
  // untouched. Foreign fields are always retained; when they exist only our enabled key
  // is retired.
  void collectDefaultCarrierCleanup(
      const ConfigService& config, bool dropCarrier, std::vector<std::vector<std::string>>& clears
  ) {
    if (!dropCarrier) {
      return;
    }
    const BarConfig* defaultBar = findBar(config.config(), kImplicitDefaultBar);
    if (defaultBar == nullptr || !defaultBar->enabled) {
      return;
    }
    if (barHasForeignContent(config, kImplicitDefaultBar, {"enabled"})) {
      clears.push_back({"bar", std::string(kImplicitDefaultBar), "enabled"});
    } else {
      clears.push_back({"bar", std::string(kImplicitDefaultBar)});
    }
  }

  // --- dedicated tray instance -------------------------------------------------

  // Override path of one field of the reserved tray instance table.
  [[nodiscard]] std::vector<std::string> trayOverridePath(std::string_view field) {
    return {"widget", std::string(kTrayInstanceId), std::string(field)};
  }

  // The closed styling projection of the dedicated tray instance: the native `tray` type
  // plus a compact recipe-colored capsule housing. Field names are exactly the
  // `[widget.*]` setting keys the settings registry declares; every value is a literal or
  // a recipe field. The global `widget.tray` entry is never part of any projection.
  [[nodiscard]] std::vector<BarFieldSpec>
  trayProjectionOf(const noctalia::shell::skins::SkinRecipe& recipe, bool joinery = true) {
    return {
        {"type", std::string(kTrayWidgetType)},       {"capsule", !joinery},
        {"capsule_fill", recipe.colorSurfaceVariant}, {"capsule_foreground", recipe.colorText},
        {"capsule_border", recipe.colorOutline},      {"capsule_border_width", kTrayCapsuleBorderWidth},
        {"capsule_padding", kTrayCapsulePadding},     {"capsule_radius", kTrayCapsuleRadius},
    };
  }

  [[nodiscard]] std::vector<BarFieldSpec> receiptTrayProjectionOf(const noctalia::shell::skins::SkinReceipt& receipt) {
    return trayProjectionOf(receipt.recipe, receipt.barGeometryRevision >= kKumihimoBarGeometryRevisionJoinery);
  }

  // One projected tray capsule-style field's native value on the owned bar, resolved the
  // same way the renderer resolves it: `resolveWidgetBarCapsuleSpec` merges the bar's
  // capsule defaults with the tray instance's own settings. Only the closed capsule
  // style field set the tray projection writes is handled, and only while the owned bar
  // is installed; anything else (or no owned bar) stays unset. Values keep the raw type
  // the projection writes — `capsule_padding` integer, `capsule_radius`/`border_width`
  // double, `capsule` boolean, the rest a color string — so the typed comparison against
  // the projected shape sees the same type.
  [[nodiscard]] std::optional<ConfigOverrideValue>
  nativeTrayCapsuleFieldValue(const Config& config, const WidgetConfig& instance, std::string_view field) {
    const BarConfig* bar = findBar(config, kStatusBarName);
    if (bar == nullptr) {
      return std::nullopt;
    }
    const WidgetBarCapsuleSpec spec = resolveWidgetBarCapsuleSpec(*bar, &instance);
    if (field == "capsule")
      return ConfigOverrideValue{spec.enabled};
    if (field == "capsule_fill")
      return ConfigOverrideValue{colorSpecToConfigString(spec.fill)};
    if (field == "capsule_foreground") {
      return spec.foreground.has_value() ? std::optional<ConfigOverrideValue>{colorSpecToConfigString(*spec.foreground)}
                                         : std::nullopt;
    }
    if (field == "capsule_border") {
      return spec.border.has_value() ? std::optional<ConfigOverrideValue>{colorSpecToConfigString(*spec.border)}
                                     : std::nullopt;
    }
    if (field == "capsule_border_width") {
      return ConfigOverrideValue{static_cast<double>(spec.borderWidth)};
    }
    if (field == "capsule_padding") {
      return ConfigOverrideValue{static_cast<std::int64_t>(spec.padding)};
    }
    if (field == "capsule_radius") {
      return spec.radius.has_value() ? std::optional<ConfigOverrideValue>{static_cast<double>(*spec.radius)}
                                     : std::nullopt;
    }
    return std::nullopt;
  }

  // The value a projected tray-instance field currently resolves to, in the same typed
  // shape the projection writes. An explicit setting is returned raw; an omitted capsule
  // style field resolves to the native value the owned bar actually renders (the override
  // layer prunes a projected value equal to the schema default, so absence is not
  // "unset"). nullopt when the instance table is absent, the owned bar is not installed,
  // or the field carries no value at all.
  [[nodiscard]] std::optional<ConfigOverrideValue>
  effectiveTrayFieldValue(const Config& config, std::string_view field) {
    const auto it = config.widgets.find(std::string(kTrayInstanceId));
    if (it == config.widgets.end()) {
      return std::nullopt;
    }
    if (field == "type") {
      return ConfigOverrideValue{it->second.type};
    }
    if (const WidgetSettingValue* value = it->second.findSetting(std::string(field)); value != nullptr) {
      if (const auto* boolean = std::get_if<bool>(value))
        return ConfigOverrideValue{*boolean};
      if (const auto* integer = std::get_if<std::int64_t>(value))
        return ConfigOverrideValue{*integer};
      if (const auto* number = std::get_if<double>(value))
        return ConfigOverrideValue{*number};
      if (const auto* text = std::get_if<std::string>(value))
        return ConfigOverrideValue{*text};
      if (const auto* list = std::get_if<std::vector<std::string>>(value)) {
        return ConfigOverrideValue{*list};
      }
      return std::nullopt;
    }
    return nativeTrayCapsuleFieldValue(config, it->second, field);
  }

  // Whether the reserved tray instance carries anything this skin did not write: an
  // override for a known widget setting outside `owned`. The instance is override-only,
  // so any such key belongs to the user and blocks removal.
  [[nodiscard]] bool trayHasForeignContent(const ConfigService& config, const std::unordered_set<std::string>& owned) {
    for (const auto& field : settings::widgetSettingSchema(std::string(kTrayWidgetType))) {
      const std::string key(field.key);
      if (owned.contains(key)) {
        continue;
      }
      if (config.hasOverride(trayOverridePath(key))) {
        return true;
      }
    }
    if (!owned.contains("type") && config.hasOverride(trayOverridePath("type"))) {
      return true;
    }
    // `enabled` lives outside the widget setting schema, so it is checked explicitly:
    // an owner's enabled=false override must survive a clear instead of being dropped
    // with the table.
    if (!owned.contains("enabled") && config.hasOverride(trayOverridePath("enabled"))) {
      return true;
    }
    return false;
  }

  // Captures the immediate pre-apply state of one tray-instance path so a receipt failure
  // can restore it. Same shape as captureLegacyBarRollback: a field path captures the
  // resolved override, the whole-table path expands to the widget schema fields plus
  // `type` and `enabled`.
  void captureTrayRollback(
      const ConfigService& config, const std::vector<std::string>& path,
      std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& rollbackSets,
      std::vector<std::vector<std::string>>& rollbackClears
  ) {
    if (path.size() < 2 || path[1] != kTrayInstanceId) {
      return;
    }
    const auto& widgets = config.config().widgets;
    const auto it = widgets.find(std::string(kTrayInstanceId));
    const WidgetConfig* instance = it != widgets.end() ? &it->second : nullptr;
    const auto captureField = [&](const std::string& field) {
      const std::vector<std::string> fieldPath{"widget", std::string(kTrayInstanceId), field};
      const std::optional<ConfigOverrideValue> resolved =
          instance != nullptr ? effectiveTrayFieldValue(config.config(), field) : std::nullopt;
      if (config.hasOverride(fieldPath) && resolved.has_value()) {
        rollbackSets.emplace_back(fieldPath, *resolved);
      } else {
        rollbackClears.push_back(fieldPath);
      }
    };
    if (path.size() == 2) {
      captureField("type");
      captureField("enabled");
      for (const auto& field : settings::widgetSettingSchema(std::string(kTrayWidgetType))) {
        captureField(std::string(field.key));
      }
      return;
    }
    captureField(path[2]);
  }

  // Retires the tray instance a receipt owned: drop the reserved table whole only while
  // every projected field still matches and the table carries nothing the skin did not
  // write; otherwise restore the fields that are still ours and leave the user-edited
  // instance in place. Same ownership rules as the owned bar.
  void collectTrayCleanup(
      const ConfigService& config, const noctalia::shell::skins::SkinReceipt& receipt,
      std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& sets,
      std::vector<std::vector<std::string>>& clears
  ) {
    if (!receipt.trayOwned || receipt.trayId != kTrayInstanceId) {
      return;
    }
    if (!config.hasOverride({"widget", std::string(kTrayInstanceId)})) {
      return;
    }
    const std::vector<BarFieldSpec> projection = receiptTrayProjectionOf(receipt);
    std::unordered_set<std::string> ownedFields;
    bool allMatch = true;
    const bool hasUserOriginals =
        std::ranges::any_of(receipt.trayOriginals, [](const noctalia::shell::skins::OwnedSetting& field) {
          return field.hadOverride;
        });
    for (const auto& spec : projection) {
      ownedFields.insert(spec.field);
      const auto current = effectiveTrayFieldValue(config.config(), spec.field);
      if (!current.has_value() || !overrideValueEquals(*current, spec.value)) {
        allMatch = false;
      }
    }
    if (allMatch && !hasUserOriginals && !trayHasForeignContent(config, ownedFields)) {
      clears.push_back({"widget", std::string(kTrayInstanceId)});
      return;
    }
    for (const auto& spec : projection) {
      const auto current = effectiveTrayFieldValue(config.config(), spec.field);
      if (!current.has_value() || !overrideValueEquals(*current, spec.value)) {
        continue;
      }
      const noctalia::shell::skins::OwnedSetting* original = findOwned(receipt.trayOriginals, spec.field);
      if (original != nullptr && original->hadOverride) {
        const auto decoded = decodeOverrideValue(nlohmann::json::parse(original->originalValue, nullptr, false));
        if (decoded.has_value()) {
          sets.emplace_back(trayOverridePath(spec.field), *decoded);
        }
      } else if (config.hasOverride(trayOverridePath(spec.field))) {
        clears.push_back(trayOverridePath(spec.field));
      }
    }
  }

  // Restores the default bar a Kumihimo receipt disabled — only for a receipt that
  // actually owns the reserved Kumihimo bar, and only while the skin's disable is still
  // in effect (the bar exists and is not enabled — a user who re-enabled it keeps it).
  // The original is restored exactly: an override's typed boolean value, or no override
  // at all when the bar had none. A malformed or non-boolean recorded original is never
  // turned into an arbitrary `enabled` write: the receipt collects nothing in that case.
  void collectDefaultBarDisableRestore(
      const ConfigService& config, const noctalia::shell::skins::SkinReceipt& receipt,
      std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& sets,
      std::vector<std::vector<std::string>>& clears
  ) {
    if (!receipt.defaultBarDisabled) {
      return;
    }
    if (!receipt.barOwned || receipt.barName != kStatusBarName || receipt.recipe.family != "kumihimo") {
      return;
    }
    const std::vector<std::string> enabledPath{"bar", std::string(kImplicitDefaultBar), "enabled"};
    const BarConfig* defaultBar = findBar(config.config(), kImplicitDefaultBar);
    if (defaultBar != nullptr && defaultBar->enabled) {
      return;
    }
    const noctalia::shell::skins::OwnedSetting& original = receipt.defaultBarEnabledOriginal;
    if (original.hadOverride) {
      const auto decoded = decodeOverrideValue(nlohmann::json::parse(original.originalValue, nullptr, false));
      if (const auto* boolean = decoded.has_value() ? std::get_if<bool>(&*decoded) : nullptr) {
        sets.emplace_back(enabledPath, ConfigOverrideValue{*boolean});
      }
      // Malformed or non-boolean: not ours to guess — neither restore nor clear.
      return;
    }
    if (config.hasOverride(enabledPath)) {
      clears.push_back(enabledPath);
    }
  }

  // The owned Kumihimo install a fresh apply writes: the bar, the dedicated tray
  // instance, and the default-bar disable, each with the per-field originals a clear
  // restores. `carriedDefault` is never set by a fresh install — Kumihimo disables the
  // default bar instead of carrying it enabled; only legacy-receipt parsing can set it.
  // Plans the owned Kumihimo bar, its dedicated tray instance and the default-bar
  // disable. A field still carrying the previous receipt's projected value keeps the
  // original that receipt recorded instead of re-capturing the skin's own write, exactly
  // like the plugin-setting originals. A pre-correction receipt is compared against the
  // legacy shape it actually wrote, so a same-recipe reapply migrates the old end lane
  // and adds the new fields without ever recording the skin's own old writes as user
  // originals.
  struct OwnedBarInstall {
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> sets;
    std::vector<noctalia::shell::skins::OwnedSetting> originals;
    bool carriedDefault = false;
    // Dedicated tray instance fields and their receipt originals.
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> traySets;
    std::vector<noctalia::shell::skins::OwnedSetting> trayOriginals;
    // Whether this install disables the implicit default bar, plus the typed `enabled`
    // original it replaces.
    bool disablesDefaultBar = false;
    noctalia::shell::skins::OwnedSetting defaultBarEnabledOriginal;
  };

  [[nodiscard]] OwnedBarInstall planOwnedBarInstall(
      const ConfigService& config, const noctalia::shell::skins::SkinRecipe& recipe,
      const std::optional<noctalia::shell::skins::SkinReceipt>& previousReceipt
  ) {
    OwnedBarInstall install;
    const std::vector<BarFieldSpec> projection = barProjectionOf(recipe);
    const BarConfig* bar = findBar(config.config(), kStatusBarName);
    const bool previousOwned =
        previousReceipt.has_value() && previousReceipt->barOwned && previousReceipt->barName == kStatusBarName;
    const std::vector<BarFieldSpec> previousProjection =
        previousOwned ? receiptBarProjectionOf(*previousReceipt) : std::vector<BarFieldSpec>{};
    for (const auto& spec : projection) {
      install.sets.emplace_back(barOverridePath(spec.field), spec.value);
      const std::optional<ConfigOverrideValue> current =
          bar != nullptr ? effectiveBarFieldValue(*bar, spec.field) : std::nullopt;
      const auto previousSpec = std::ranges::find(previousProjection, spec.field, &BarFieldSpec::field);
      if (previousOwned
          && current.has_value()
          && previousSpec != previousProjection.end()
          && overrideValueEquals(*current, previousSpec->value)) {
        const noctalia::shell::skins::OwnedSetting* previousOriginal =
            findOwned(previousReceipt->barOriginals, spec.field);
        install.originals.push_back(
            previousOriginal != nullptr ? *previousOriginal : noctalia::shell::skins::OwnedSetting{.key = spec.field}
        );
        continue;
      }
      noctalia::shell::skins::OwnedSetting original;
      original.key = spec.field;
      original.hadOverride = config.hasOverride(barOverridePath(spec.field));
      if (original.hadOverride && current.has_value()) {
        original.originalValue = overrideValueJson(*current).dump();
      }
      install.originals.push_back(std::move(original));
    }

    // Dedicated tray instance: the reserved table is written fresh (or rewritten with the
    // carried originals of a previous owned tray) alongside the bar, so the end lane's
    // reference always resolves to the native tray type.
    const bool previousTrayOwned =
        previousReceipt.has_value() && previousReceipt->trayOwned && previousReceipt->trayId == kTrayInstanceId;
    const std::vector<BarFieldSpec> previousTrayProjection =
        previousTrayOwned ? receiptTrayProjectionOf(*previousReceipt) : std::vector<BarFieldSpec>{};
    for (const auto& spec : trayProjectionOf(recipe)) {
      install.traySets.emplace_back(trayOverridePath(spec.field), spec.value);
      const std::optional<ConfigOverrideValue> current = effectiveTrayFieldValue(config.config(), spec.field);
      const auto previousSpec = std::ranges::find(previousTrayProjection, spec.field, &BarFieldSpec::field);
      if (previousTrayOwned
          && current.has_value()
          && previousSpec != previousTrayProjection.end()
          && overrideValueEquals(*current, previousSpec->value)) {
        const noctalia::shell::skins::OwnedSetting* previousOriginal =
            findOwned(previousReceipt->trayOriginals, spec.field);
        install.trayOriginals.push_back(
            previousOriginal != nullptr ? *previousOriginal : noctalia::shell::skins::OwnedSetting{.key = spec.field}
        );
        continue;
      }
      noctalia::shell::skins::OwnedSetting original;
      original.key = spec.field;
      original.hadOverride = config.hasOverride(trayOverridePath(spec.field));
      if (original.hadOverride && current.has_value()) {
        original.originalValue = overrideValueJson(*current).dump();
      }
      install.trayOriginals.push_back(std::move(original));
    }

    // Default bar: Kumihimo disables it — reversibly — for as long as the skin is
    // applied. The typed `enabled` original is captured before the write: an override's
    // current value, or nothing when no override existed (the shell's implicit default).
    // A previous receipt's own disable is carried instead of re-captured while its write
    // is still in effect; a user who re-enabled the bar gets a fresh capture. A default
    // bar the user disabled themselves (enabled=false with no receipt behind it) is left
    // exactly as it is — this skin owns no disable then.
    const std::vector<std::string> defaultEnabledPath{"bar", std::string(kImplicitDefaultBar), "enabled"};
    const BarConfig* defaultBar = findBar(config.config(), kImplicitDefaultBar);
    const bool previousDisabled = previousReceipt.has_value() && previousReceipt->defaultBarDisabled;
    const bool currentlyDisabled = defaultBar != nullptr && !defaultBar->enabled;
    if (currentlyDisabled && previousDisabled) {
      install.disablesDefaultBar = true;
      install.defaultBarEnabledOriginal = previousReceipt->defaultBarEnabledOriginal;
      install.sets.emplace_back(defaultEnabledPath, ConfigOverrideValue{false});
    } else if (!currentlyDisabled) {
      noctalia::shell::skins::OwnedSetting original;
      original.key = "enabled";
      if (previousReceipt.has_value()
          && previousReceipt->barCarriedDefault
          && defaultBar != nullptr
          && defaultBar->enabled) {
        // The enabled=true carrier write of a legacy receipt is still the skin's own:
        // the disable replaces it, and the true original is "no override" — the bar was
        // implicit before this skin ever touched it.
        original.hadOverride = false;
      } else {
        original.hadOverride = config.hasOverride(defaultEnabledPath);
        if (original.hadOverride && defaultBar != nullptr) {
          original.originalValue = overrideValueJson(ConfigOverrideValue{defaultBar->enabled}).dump();
        }
      }
      install.disablesDefaultBar = true;
      install.defaultBarEnabledOriginal = std::move(original);
      install.sets.emplace_back(defaultEnabledPath, ConfigOverrideValue{false});
    }
    return install;
  }

  // Captures the immediate pre-apply state of one legacy bar path so a receipt failure
  // can restore it alongside the plugin-key rollback. Field-level paths capture the
  // resolved field when an override exists (cleared paths included); a whole-table path
  // expands to every schema bar field plus the monitor override, each captured the same
  // way, so restoring the rollback reproduces exactly the previous explicit owned state.
  void captureLegacyBarRollback(
      const ConfigService& config, const std::vector<std::string>& path,
      std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>>& rollbackSets,
      std::vector<std::vector<std::string>>& rollbackClears
  ) {
    if (path.size() < 2) {
      return;
    }
    const BarConfig* bar = findBar(config.config(), path[1]);
    const auto captureField = [&](const std::string& field) {
      const std::vector<std::string> fieldPath{path[0], path[1], field};
      const std::optional<ConfigOverrideValue> resolved =
          bar != nullptr ? effectiveBarFieldValue(*bar, field) : std::nullopt;
      if (config.hasOverride(fieldPath) && resolved.has_value()) {
        rollbackSets.emplace_back(fieldPath, *resolved);
      } else {
        rollbackClears.push_back(fieldPath);
      }
    };
    if (path.size() == 2) {
      for (const auto& field : noctalia::config::schema::barFieldsSchema()) {
        captureField(std::string(field.key));
      }
      captureField("monitor");
      return;
    }
    captureField(path[2]);
  }

  [[nodiscard]] nlohmann::json recipeJson(const noctalia::shell::skins::SkinRecipe& recipe) {
    return nlohmann::json{
        {"schema_version", 1},
        {"target", "noctalia"},
        {"variant_id", recipe.variantId},
        {"content_id", recipe.contentId},
        {"name", recipe.name},
        {"family", recipe.family},
        {"topology", recipe.topology},
        {"colors",
         nlohmann::json{
             {"surface", recipe.colorSurface},
             {"surface_variant", recipe.colorSurfaceVariant},
             {"text", recipe.colorText},
             {"muted", recipe.colorMuted},
             {"accent", recipe.colorAccent},
             {"outline", recipe.colorOutline},
         }},
        {"density", recipe.density},
        {"preview_path", recipe.previewPath},
    };
  }

  [[nodiscard]] std::string refusalJson(std::string_view error) {
    return nlohmann::json{{"ok", false}, {"error", std::string(error)}}.dump() + "\n";
  }

  [[nodiscard]] std::string successJson(const noctalia::shell::skins::SkinRecipe& recipe) {
    return nlohmann::json{{"ok", true}, {"active", recipeJson(recipe)}}.dump() + "\n";
  }

  // Strict mirror of the backend recipe v1 schema. Every rejection happens here, before
  // any config mutation, so a bad recipe can never leave a partial skin behind.
  [[nodiscard]] std::optional<noctalia::shell::skins::SkinRecipe>
  parseRecipe(std::string_view text, std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<noctalia::shell::skins::SkinRecipe> {
      if (error != nullptr) {
        *error = std::move(message);
      }
      return std::nullopt;
    };

    nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
      return fail("recipe is not valid JSON");
    }
    if (!doc.is_object()) {
      return fail("recipe must be a JSON object");
    }
    for (const auto& [key, value] : doc.items()) {
      (void)value;
      if (!std::ranges::contains(kRecipeKeys, std::string_view(key))) {
        return fail("unknown recipe key \"" + key + "\"");
      }
    }
    for (const std::string_view key : kRecipeKeys) {
      if (!doc.contains(std::string(key))) {
        return fail("missing recipe key \"" + std::string(key) + "\"");
      }
    }
    if (!doc["schema_version"].is_number_integer() || doc["schema_version"].get<std::int64_t>() != 1) {
      return fail("schema_version must be 1");
    }
    if (!doc["target"].is_string() || doc["target"].get<std::string>() != "noctalia") {
      return fail("target must be \"noctalia\"");
    }

    const auto getString = [&doc](const char* key, std::string& out) {
      if (!doc[key].is_string()) {
        return false;
      }
      out = doc[key].get<std::string>();
      return true;
    };

    noctalia::shell::skins::SkinRecipe recipe;
    if (!getString("variant_id", recipe.variantId)) {
      return fail("variant_id must be a string");
    }
    if (!isLowerHex(recipe.variantId, 32)) {
      return fail("variant_id must be 32 lowercase hex characters");
    }
    if (!getString("content_id", recipe.contentId)) {
      return fail("content_id must be a string");
    }
    if (!isLowerHex(recipe.contentId, 64)) {
      return fail("content_id must be 64 lowercase hex characters");
    }
    if (!getString("name", recipe.name)) {
      return fail("name must be a string");
    }
    if (recipe.name.empty() || recipe.name.size() > kMaxNameBytes || hasControlChar(recipe.name)) {
      return fail("name must be 1..100 bytes without control characters");
    }
    if (!getString("family", recipe.family)) {
      return fail("family must be a string");
    }
    if (!std::ranges::contains(kKnownFamilies, std::string_view(recipe.family))) {
      return fail("unknown family \"" + recipe.family + "\"");
    }
    if (!getString("topology", recipe.topology)) {
      return fail("topology must be a string");
    }
    if (!std::ranges::contains(kTopologies, std::string_view(recipe.topology))) {
      return fail("unknown topology \"" + recipe.topology + "\"");
    }
    if (!getString("density", recipe.density)) {
      return fail("density must be a string");
    }
    if (!std::ranges::contains(kDensities, std::string_view(recipe.density))) {
      return fail(R"(density must be "compact" or "comfortable")");
    }
    if (!getString("preview_path", recipe.previewPath)) {
      return fail("preview_path must be a string");
    }
    // Absolute local path only: no scheme, no control characters, bounded length. The
    // path is stored, never opened.
    if (recipe.previewPath.empty()
        || recipe.previewPath.front() != '/'
        || recipe.previewPath.size() > kMaxPreviewPathBytes
        || recipe.previewPath.contains("://")
        || hasControlChar(recipe.previewPath)) {
      return fail("preview_path must be an absolute local path");
    }

    const nlohmann::json& colors = doc["colors"];
    if (!colors.is_object()) {
      return fail("colors must be an object");
    }
    for (const auto& [key, value] : colors.items()) {
      (void)value;
      if (!std::ranges::contains(kColorKeys, std::string_view(key))) {
        return fail("unknown colors key \"" + key + "\"");
      }
    }
    for (const std::string_view key : kColorKeys) {
      if (!colors.contains(std::string(key))) {
        return fail("missing colors key \"" + std::string(key) + "\"");
      }
    }
    const auto readColor = [&colors, &fail](std::string_view key, std::string& out) {
      const nlohmann::json& value = colors[std::string(key)];
      if (!value.is_string()) {
        fail("colors." + std::string(key) + " must be a string");
        return false;
      }
      const std::string colorText = value.get<std::string>();
      if (!isColorHex(colorText)) {
        fail("colors." + std::string(key) + " must be #rrggbb");
        return false;
      }
      out = toLowerAscii(colorText);
      return true;
    };
    if (!readColor("surface", recipe.colorSurface)
        || !readColor("surface_variant", recipe.colorSurfaceVariant)
        || !readColor("text", recipe.colorText)
        || !readColor("muted", recipe.colorMuted)
        || !readColor("accent", recipe.colorAccent)
        || !readColor("outline", recipe.colorOutline)) {
      return std::nullopt;
    }

    // Family/topology compatibility first, then availability: an unsupported direction
    // is refused with the same closed door as a malformed one.
    if (recipe.family == "stagecraft") {
      if (!std::ranges::contains(kStagecraftTopologies, std::string_view(recipe.topology))) {
        return fail("topology \"" + recipe.topology + "\" is not a Stagecraft topology");
      }
    } else if (recipe.family == "kumihimo") {
      if (!std::ranges::contains(kKumihimoTopologies, std::string_view(recipe.topology))) {
        return fail("topology \"" + recipe.topology + "\" is not a Kumihimo topology");
      }
    } else if (recipe.topology != "compact-rail") {
      return fail("family \"" + recipe.family + R"(" requires topology "compact-rail")");
    }
    if (!std::ranges::contains(kSupportedFamilies, std::string_view(recipe.family))) {
      return fail("family \"" + recipe.family + "\" is not available yet");
    }
    return recipe;
  }

  [[nodiscard]] bool pluginLoaded(std::string_view pluginId) {
    auto& registry = scripting::PluginRegistry::instance();
    registry.ensureScanned();
    return registry.findManifest(pluginId) != nullptr;
  }

  // Root directory that holds the packaged presentation plugin as `<root>/skin-runtime/`.
  // An explicit dev override wins so a dev run can point at the worktree's integrations
  // directory; otherwise it is the installed assets root. No absolute home is embedded.
  [[nodiscard]] std::string packagedPluginsRoot() {
    if (const char* override = std::getenv("NOCTALIA_SKIN_PLUGIN_SOURCE"); override != nullptr && override[0] != '\0') {
      return override;
    }
    return paths::assetPath("plugins").string();
  }

  [[nodiscard]] std::optional<nlohmann::json> parseStateObject(const std::optional<std::string>& raw) {
    if (!raw.has_value()) {
      return std::nullopt;
    }
    nlohmann::json doc = nlohmann::json::parse(*raw, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
      return std::nullopt;
    }
    return doc;
  }

  void
  copyStringAs(const nlohmann::json& source, std::string_view key, std::string_view targetKey, nlohmann::json& out) {
    const auto it = source.find(std::string(key));
    if (it != source.end() && it->is_string()) {
      out[std::string(targetKey)] = it->get<std::string>();
    }
  }

  void copyString(const nlohmann::json& source, std::string_view key, nlohmann::json& out) {
    copyStringAs(source, key, key, out);
  }

  void copyNumber(const nlohmann::json& source, std::string_view key, nlohmann::json& out) {
    const auto it = source.find(std::string(key));
    if (it == source.end() || !it->is_number()) {
      return;
    }
    if (it->is_number_integer()) {
      out[std::string(key)] = it->get<std::int64_t>();
    } else if (it->is_number_unsigned()) {
      out[std::string(key)] = it->get<std::uint64_t>();
    } else {
      out[std::string(key)] = it->get<double>();
    }
  }

  void copyBool(const nlohmann::json& source, std::string_view key, nlohmann::json& out) {
    const auto it = source.find(std::string(key));
    if (it != source.end() && it->is_boolean()) {
      out[std::string(key)] = it->get<bool>();
    }
  }

  // Joins a JSON array of strings into one display string. nullopt unless the node is a
  // non-empty array whose every element is a string, so a malformed or mixed source is
  // omitted rather than half-published. q/tmus queue rows carry `artists` in this shape.
  [[nodiscard]] std::optional<std::string> joinedStrings(const nlohmann::json& node) {
    if (!node.is_array()) {
      return std::nullopt;
    }
    std::string joined;
    for (const auto& item : node) {
      if (!item.is_string()) {
        return std::nullopt;
      }
      if (!joined.empty()) {
        joined += ", ";
      }
      joined += item.get_ref<const std::string&>();
    }
    if (joined.empty()) {
      return std::nullopt;
    }
    return joined;
  }

  // Curated now-playing summary sourced from the existing q/tmus service state. Reads
  // two published keys, never polls anything, and omits absent fields instead of
  // inventing zeroes. Null when the plugin is disabled or has published nothing.
  [[nodiscard]] std::optional<nlohmann::json> musicSummary() {
    if (!pluginLoaded(kTmusPluginId)) {
      return std::nullopt;
    }
    auto& store = scripting::PluginStateStore::instance();
    const auto heavy = parseStateObject(store.get(std::string(kTmusPluginId), "tmus"));
    const auto now = parseStateObject(store.get(std::string(kTmusPluginId), "tmus.now"));
    if (!heavy.has_value() && !now.has_value()) {
      return std::nullopt;
    }

    nlohmann::json out = nlohmann::json::object();
    if (heavy.has_value()) {
      copyString(*heavy, "title", out);
      copyString(*heavy, "artist", out);
      copyString(*heavy, "artPath", out);
      copyString(*heavy, "status", out);
      copyNumber(*heavy, "position", out);
      copyNumber(*heavy, "duration", out);
      copyNumber(*heavy, "volume", out);
      copyBool(*heavy, "linked", out);
      copyBool(*heavy, "shuffle", out);
      copyString(*heavy, "loop", out);
      copyNumber(*heavy, "at", out);
      // Current-track identity plus this one track's own rating — never the ratings map.
      // `trackId` is q/tmus's "provider:id" string; the rating is looked up by that exact
      // key. Both are omitted when the id is absent/empty or the value is not a string.
      if (const auto trackIdIt = heavy->find("trackId");
          trackIdIt != heavy->end() && trackIdIt->is_string() && !trackIdIt->get_ref<const std::string&>().empty()) {
        const auto& trackId = trackIdIt->get_ref<const std::string&>();
        out["trackId"] = trackId;
        if (const auto ratingsIt = heavy->find("ratings"); ratingsIt != heavy->end() && ratingsIt->is_object()) {
          if (const auto ratingIt = ratingsIt->find(trackId); ratingIt != ratingsIt->end() && ratingIt->is_string()) {
            out["rating"] = ratingIt->get<std::string>();
          }
        }
      }
    }
    // The lightweight key is fresher for transport fields, so it wins over the snapshot.
    if (now.has_value()) {
      copyBool(*now, "linked", out);
      copyString(*now, "status", out);
      copyNumber(*now, "position", out);
      copyNumber(*now, "duration", out);
      copyNumber(*now, "volume", out);
      copyNumber(*now, "at", out);
    }
    if (heavy.has_value()) {
      if (const auto queueIt = heavy->find("queue"); queueIt != heavy->end() && queueIt->is_array()) {
        nlohmann::json queue = nlohmann::json::array();
        for (const auto& item : *queueIt) {
          if (queue.size() >= kMaxQueueEntries) {
            break;
          }
          if (!item.is_object()) {
            continue;
          }
          nlohmann::json entry = nlohmann::json::object();
          copyString(item, "title", entry);
          // The queue rows carry "art_path"; publish the consumer's "artPath".
          copyStringAs(item, "art_path", "artPath", entry);
          // Real source types only: q/tmus queue rows carry `artists` as a string array
          // and a numeric `duration` (seconds, the same unit the top-level duration
          // uses). A non-array artists value or a non-numeric duration is omitted.
          if (const auto artistsIt = item.find("artists"); artistsIt != item.end()) {
            if (const auto artist = joinedStrings(*artistsIt); artist.has_value()) {
              entry["artist"] = *artist;
            }
          }
          copyNumber(item, "duration", entry);
          if (!entry.empty()) {
            queue.push_back(std::move(entry));
          }
        }
        if (!queue.empty()) {
          out["queue"] = std::move(queue);
        }
      }
    }
    if (out.empty()) {
      return std::nullopt;
    }
    return out;
  }

  // Curated AI summary sourced from the existing q/ai-triage service state. Strictly an
  // allowlist: triage counts, freshness, and safe usage quota/balance rows. Log lines,
  // tail text, bot control state, and action/model paths are never read, so neither
  // secret values nor log content can cross this boundary.
  [[nodiscard]] std::optional<nlohmann::json> aiSummary() {
    if (!pluginLoaded(kAiTriagePluginId)) {
      return std::nullopt;
    }
    const auto data =
        parseStateObject(scripting::PluginStateStore::instance().get(std::string(kAiTriagePluginId), "ai_triage"));
    if (!data.has_value()) {
      return std::nullopt;
    }

    nlohmann::json out = nlohmann::json::object();
    nlohmann::json counts = nlohmann::json::object();
    for (const char* key : {"new", "unsure", "warn", "yes", "no", "unread", "total"}) {
      copyNumber(*data, key, counts);
    }
    if (!counts.empty()) {
      out["counts"] = std::move(counts);
    }
    copyNumber(*data, "at", out);
    copyBool(*data, "running", out);

    if (const auto usageIt = data->find("usage"); usageIt != data->end() && usageIt->is_object()) {
      nlohmann::json usage = nlohmann::json::object();
      copyNumber(*usageIt, "at", usage);
      copyString(*usageIt, "error", usage);
      copyNumber(*usageIt, "worst", usage);
      copyBool(*usageIt, "busy", usage);
      if (const auto providersIt = usageIt->find("providers");
          providersIt != usageIt->end() && providersIt->is_array()) {
        nlohmann::json providers = nlohmann::json::array();
        for (const auto& provider : *providersIt) {
          if (!provider.is_object()) {
            continue;
          }
          nlohmann::json entry = nlohmann::json::object();
          copyString(provider, "id", entry);
          copyString(provider, "title", entry);
          copyString(provider, "plan", entry);
          copyString(provider, "error", entry);
          if (const auto rowsIt = provider.find("rows"); rowsIt != provider.end() && rowsIt->is_array()) {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& row : *rowsIt) {
              if (!row.is_object()) {
                continue;
              }
              nlohmann::json safeRow = nlohmann::json::object();
              copyString(row, "label", safeRow);
              copyNumber(row, "fraction", safeRow);
              copyString(row, "text", safeRow);
              copyNumber(row, "resetsAt", safeRow);
              if (!safeRow.empty()) {
                rows.push_back(std::move(safeRow));
              }
            }
            if (!rows.empty()) {
              entry["rows"] = std::move(rows);
            }
          }
          if (!entry.empty()) {
            providers.push_back(std::move(entry));
          }
        }
        if (!providers.empty()) {
          usage["providers"] = std::move(providers);
        }
      }
      if (!usage.empty()) {
        out["usage"] = std::move(usage);
      }
    }
    return out;
  }

  [[nodiscard]] std::optional<std::string> findSetting(const SettingList& settings, std::string_view key) {
    for (const auto& [settingKey, value] : settings) {
      if (settingKey == key) {
        return value;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::string receiptJson(const noctalia::shell::skins::SkinReceipt& receipt) {
    nlohmann::json originalEntries = nlohmann::json::array();
    for (const auto& original : receipt.originals) {
      originalEntries.push_back(
          nlohmann::json{
              {"key", original.key},
              {"had_override", original.hadOverride},
              {"original_value", original.originalValue},
          }
      );
    }
    nlohmann::json barFields = nlohmann::json::array();
    for (const auto& original : receipt.barOriginals) {
      nlohmann::json value = nullptr;
      if (original.hadOverride) {
        nlohmann::json parsed = nlohmann::json::parse(original.originalValue, nullptr, false);
        if (!parsed.is_discarded()) {
          value = std::move(parsed);
        }
      }
      barFields.push_back(
          nlohmann::json{
              {"field", original.key},
              {"had_override", original.hadOverride},
              {"value", std::move(value)},
          }
      );
    }
    nlohmann::json trayFields = nlohmann::json::array();
    for (const auto& original : receipt.trayOriginals) {
      nlohmann::json value = nullptr;
      if (original.hadOverride) {
        nlohmann::json parsed = nlohmann::json::parse(original.originalValue, nullptr, false);
        if (!parsed.is_discarded()) {
          value = std::move(parsed);
        }
      }
      trayFields.push_back(
          nlohmann::json{
              {"field", original.key},
              {"had_override", original.hadOverride},
              {"value", std::move(value)},
          }
      );
    }
    nlohmann::json defaultEnabledValue = nullptr;
    if (receipt.defaultBarEnabledOriginal.hadOverride) {
      nlohmann::json parsed = nlohmann::json::parse(receipt.defaultBarEnabledOriginal.originalValue, nullptr, false);
      if (!parsed.is_discarded()) {
        defaultEnabledValue = std::move(parsed);
      }
    }
    nlohmann::json receiptDoc{
        {"schema_version", 1},
        {"recipe", recipeJson(receipt.recipe)},
        {"originals", std::move(originalEntries)},
        // Registration the skin performed, so a later clear reverts exactly that.
        {"plugin",
         nlohmann::json{
             {"added_source", receipt.addedSource},
             {"enabled_plugin", receipt.enabledPlugin},
         }},
        // The dedicated bar this skin created, with the per-field originals a clear
        // restores when the bar itself was edited and must stay.
        {"bar",
         nlohmann::json{
             {"owned", receipt.barOwned},
             {"name", receipt.barName},
             {"geometry_revision", receipt.barGeometryRevision},
             {"carried_default", receipt.barCarriedDefault},
             {"fields", std::move(barFields)},
         }},
        // The dedicated native tray instance and the default-bar disable, each with the
        // same receipt-scoped originals-and-rollback ownership as the bar.
        {"tray",
         nlohmann::json{
             {"owned", receipt.trayOwned},
             {"id", receipt.trayId},
             {"fields", std::move(trayFields)},
         }},
        {"default_bar",
         nlohmann::json{
             {"disabled", receipt.defaultBarDisabled},
             {"enabled_original",
              nlohmann::json{
                  {"had_override", receipt.defaultBarEnabledOriginal.hadOverride},
                  {"value", std::move(defaultEnabledValue)},
              }},
         }},
    };
    return receiptDoc.dump();
  }

  // --- skin-status surface_catalog -------------------------------------------
  //
  // A read-only projection of the shell surfaces a whole-skin package could cover.
  // Built exclusively on `noctalia msg skin-status`; it never touches the hot
  // layout path and never mutates config, the plugin registry, or panel state.
  // Only display metadata is projected: no settings values, file paths, commands,
  // outputs, secrets, or live runtime state. Registry pointers are consumed inside
  // this single call and never stored; `ensureScanned` is the documented idempotent
  // entry point and is the only registry call that may scan (never a forced rescan).

  // Bounded-contract limits mirroring the presentation catalog schema.
  constexpr std::size_t kMaxSurfaceTextBytes = 256;
  constexpr std::size_t kMaxSurfaceNameBytes = 512;
  constexpr std::size_t kMaxCatalogSurfaces = 128;
  constexpr std::size_t kMaxCatalogNonvisual = 256;

  // Truncates to at most `maxBytes` UTF-8 bytes on a code-point boundary. Oversized
  // manifest strings are clipped rather than rejected, so the inventory stays a
  // valid bounded catalog instead of failing the whole status payload.
  [[nodiscard]] std::string boundedUtf8(std::string_view text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) {
      return std::string(text);
    }
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
      --cut;
    }
    return std::string(text.substr(0, cut));
  }

  // Display label for a plugin entry, mirroring the settings UI convention: the
  // manifest name, plus the entry id when the plugin ships more than one entry of
  // that kind so same-plugin entries stay distinguishable.
  [[nodiscard]] std::string pluginEntryLabel(const scripting::ResolvedPluginEntry& resolved) {
    if (resolved.manifest == nullptr || resolved.entry == nullptr || resolved.manifest->name.empty()) {
      return resolved.fullId();
    }
    const auto sameKind =
        std::ranges::count_if(resolved.manifest->entries, [&resolved](const scripting::PluginEntry& entry) {
          return entry.kind == resolved.entry->kind;
        });
    if (sameKind > 1) {
      return resolved.manifest->name + " · " + resolved.entry->id;
    }
    return resolved.manifest->name;
  }

  [[nodiscard]] nlohmann::json surfaceCatalog(
      const ConfigService& configService, const PanelManager& panels,
      const std::optional<noctalia::shell::skins::SkinRecipe>& active
  ) {
    auto& registry = scripting::PluginRegistry::instance();
    registry.ensureScanned();

    const Config& config = configService.config();
    std::map<std::string, nlohmann::json> surfaces;
    std::map<std::string, nlohmann::json> nonvisual;
    bool complete = true;
    const auto validIdentity = [](std::string_view value) {
      return value.size() <= kMaxSurfaceTextBytes
          && std::ranges::none_of(value, [](unsigned char ch) { return ch < 0x20U || ch == 0x7FU; });
    };
    const auto displayName = [](std::string name) {
      std::erase_if(name, [](unsigned char ch) { return ch < 0x20U || ch == 0x7FU; });
      return boundedUtf8(name, kMaxSurfaceNameBytes);
    };

    // Conservative runtime support. "Implemented" is claimed only for surfaces this
    // runtime actually governs.
    //
    // Stagecraft: the native bars (styled by the common chrome profile, lanes untouched),
    // the native launcher panel, q/skin-runtime's own entries, and — while a Stagecraft
    // profile is active — the adapted panel/bar-widget/plugin-widget surfaces of q/tmus,
    // q/ai-triage and q/wallpaper-studio.
    //
    // Kumihimo: the owned bar and its monitoring/music/clock entries, tmus panel,
    // and the independent AI entry/cassette. Other bars, launcher, skin-runtime
    // panels and Studio remain concept-only; a palette alone is not a redesign.
    const auto supportFor = [&](const std::string& kind, const std::string& pluginId, const std::string& id) {
      if (active.has_value() && active->family == "kumihimo") {
        if (kind == "bar") {
          return std::string(id == "bar:" + std::string(kStatusBarName) ? "implemented" : "concept-only");
        }
        if ((kind == "bar-widget" || kind == "plugin-widget")
            && pluginId == std::string(kPluginId)
            && (id.ends_with(std::string(kStatusBarWidgetId))
                || id.ends_with(std::string(kMusicBarWidgetId))
                || id.ends_with("q/skin-runtime:bar-clock"))) {
          return std::string("implemented");
        }
        if (kind == "panel" && pluginId == std::string(kTmusPluginId)) {
          return std::string("implemented");
        }
        if ((kind == "panel" || kind == "bar-widget" || kind == "plugin-widget")
            && pluginId == std::string(kAiTriagePluginId)) {
          return std::string("implemented");
        }
        return std::string("concept-only");
      }
      if (kind == "bar") {
        return std::string("implemented");
      }
      if (id == "panel:launcher") {
        return std::string("implemented");
      }
      if (pluginId == std::string(kPluginId)) {
        return std::string("implemented");
      }
      // The three adapted presentations own their widget and panel surfaces only; a
      // desktop-widget entry of theirs, if one ever ships, stays concept-only until it
      // actually receives the family treatment.
      if (active.has_value()
          && active->family == "stagecraft"
          && (kind == "panel" || kind == "bar-widget" || kind == "plugin-widget")
          && (pluginId == std::string(kTmusPluginId)
              || pluginId == std::string(kAiTriagePluginId)
              || pluginId == std::string(kWallpaperStudioPluginId))) {
        return std::string("implemented");
      }
      return std::string("concept-only");
    };

    const auto addSurface = [&](const std::string& rawId, std::string name, std::string kind, std::string pluginId,
                                std::string entryId, std::string type, bool persistent, std::string runtimeSupport) {
      if (!validIdentity(rawId) || !validIdentity(pluginId) || !validIdentity(entryId) || !validIdentity(type)) {
        complete = false;
        return;
      }
      if (surfaces.contains(rawId)) {
        return;
      }
      if (surfaces.size() >= kMaxCatalogSurfaces) {
        complete = false;
        return;
      }
      const std::string& id = rawId;
      if (name.empty()) {
        name = rawId;
      }
      surfaces.emplace(
          id,
          nlohmann::json{
              {"id", id},
              {"name", displayName(name)},
              {"kind", boundedUtf8(kind, kMaxSurfaceTextBytes)},
              {"plugin_id", boundedUtf8(pluginId, kMaxSurfaceTextBytes)},
              {"entry_id", boundedUtf8(entryId, kMaxSurfaceTextBytes)},
              {"type", type},
              {"persistent", persistent},
              {"runtime_support", boundedUtf8(runtimeSupport, kMaxSurfaceTextBytes)},
          }
      );
    };

    // Resolve each lane token and preserve its configured instance identity.
    // Available plugin entry previews are cataloged separately below.
    const auto addBarWidget = [&](const std::string& barName, const std::string& reference) {
      std::string type = settings::widgetTypeForReference(config, reference);
      if (type.empty()) {
        // A lane may name a plugin widget entry directly (the picker inserts the
        // full entry id without a `[widget.*]` table).
        const auto direct = registry.resolve(reference);
        if (direct.has_value() && direct->entry->kind == scripting::PluginEntryKind::Widget) {
          type = reference;
        }
      }
      if (type.empty()) {
        return; // Not a configured or built-in widget: nothing is actually rendered.
      }
      if (const auto resolved = registry.resolve(type);
          resolved.has_value() && resolved->entry->kind == scripting::PluginEntryKind::Widget) {
        const std::string fullId = resolved->fullId();
        const std::string id = "bar-widget:" + barName + ":" + reference;
        addSurface(
            id, pluginEntryLabel(*resolved), "bar-widget", resolved->manifest->id, resolved->entry->id, fullId, true,
            supportFor("bar-widget", resolved->manifest->id, id)
        );
        return;
      }
      addSurface(
          "bar-widget:" + barName + ":" + reference, settings::widgetReferenceInfo(config, reference).title,
          "bar-widget", std::string{}, std::string{}, type, true, supportFor("bar-widget", std::string{}, std::string{})
      );
    };

    // Expands one lane's tokens: capsule group tokens are replaced by their enabled
    // group's members (the group itself is not a rendered widget).
    const auto addLane = [&](const std::string& barName, const std::vector<std::string>& lane,
                             const std::vector<BarCapsuleGroupStyle>& groups) {
      for (const std::string& token : lane) {
        if (isCapsuleGroupToken(token)) {
          const std::string groupId = capsuleGroupTokenId(token);
          const auto group = std::ranges::find(groups, groupId, &BarCapsuleGroupStyle::id);
          if (group == groups.end() || !group->enabled) {
            continue;
          }
          for (const std::string& member : group->members) {
            addBarWidget(barName, member);
          }
          continue;
        }
        addBarWidget(barName, token);
      }
    };

    // Bars: base lanes plus every per-monitor lane override, with the bar's groups
    // unless the override supplies its own. Mirrors the host's barMayRender /
    // effective-lane merge without depending on either private helper.
    for (const BarConfig& bar : config.bars) {
      const bool baseEnabled = bar.enabled;
      bool anyMonitorEnabled = false;
      for (const BarMonitorOverride& ovr : bar.monitorOverrides) {
        if (ovr.enabled.value_or(false)) {
          anyMonitorEnabled = true;
          break;
        }
      }
      if (!baseEnabled && !anyMonitorEnabled) {
        continue;
      }
      const std::string barId = "bar:" + bar.name;
      addSurface(
          barId, bar.name, "bar", std::string{}, std::string{}, "bar", true, supportFor("bar", std::string{}, barId)
      );
      if (baseEnabled) {
        addLane(bar.name, bar.startWidgets, bar.widgetCapsuleGroups);
        addLane(bar.name, bar.centerWidgets, bar.widgetCapsuleGroups);
        addLane(bar.name, bar.endWidgets, bar.widgetCapsuleGroups);
      }
      for (const BarMonitorOverride& ovr : bar.monitorOverrides) {
        if (!ovr.enabled.value_or(baseEnabled)) {
          continue;
        }
        const std::vector<BarCapsuleGroupStyle>& groups =
            ovr.widgetCapsuleGroups ? *ovr.widgetCapsuleGroups : bar.widgetCapsuleGroups;
        addLane(bar.name, ovr.startWidgets ? *ovr.startWidgets : bar.startWidgets, groups);
        addLane(bar.name, ovr.centerWidgets ? *ovr.centerWidgets : bar.centerWidgets, groups);
        addLane(bar.name, ovr.endWidgets ? *ovr.endWidgets : bar.endWidgets, groups);
      }
    }

    // Configured desktop/lockscreen widgets retain their instance identity even
    // when several placements use the same plugin entry.
    const auto addDesktopWidget = [&](const DesktopWidgetState& state, bool lockscreen) {
      if (const auto resolved = registry.resolve(state.type);
          resolved.has_value() && resolved->entry->kind == scripting::PluginEntryKind::DesktopWidget) {
        const std::string fullId = resolved->fullId();
        const std::string kind = lockscreen ? "lockscreen-widget" : "desktop-widget";
        const std::string id = kind + ":" + state.id;
        addSurface(
            id, pluginEntryLabel(*resolved), kind, resolved->manifest->id, resolved->entry->id, fullId, true,
            supportFor(kind, resolved->manifest->id, id)
        );
        return;
      }
      const std::string kind = lockscreen ? "lockscreen-widget" : "desktop-widget";
      addSurface(
          kind + ":" + state.id, settings::titleFromWidgetKey(state.type), kind, std::string{}, std::string{},
          state.type, true, supportFor(kind, std::string{}, std::string{})
      );
    };
    if (config.desktopWidgets.enabled) {
      for (const DesktopWidgetState& state : config.desktopWidgets.widgets) {
        if (state.enabled) {
          addDesktopWidget(state, false);
        }
      }
    }
    if (config.lockscreenWidgets.enabled) {
      for (const DesktopWidgetState& state : config.lockscreenWidgets.widgets) {
        if (state.enabled) {
          addDesktopWidget(state, true);
        }
      }
    }

    // Panels: everything currently registered, plus every [[panel]] entry of an
    // enabled plugin (deduped), so a known-but-not-yet-open panel is still cataloged.
    {
      std::set<std::string> panelIds;
      for (const std::string& panelId : panels.availablePanelIds()) {
        panelIds.insert(panelId);
      }
      for (const scripting::ResolvedPluginEntry& resolved : registry.entriesOfKind(scripting::PluginEntryKind::Panel)) {
        panelIds.insert(resolved.fullId());
      }
      for (const std::string& panelId : panelIds) {
        std::string pluginId;
        std::string entryId;
        if (const auto colon = panelId.find(':'); colon != std::string::npos) {
          pluginId = panelId.substr(0, colon);
          entryId = panelId.substr(colon + 1);
        }
        bool persistent = false;
        if (const auto resolved = registry.resolve(panelId);
            resolved.has_value() && resolved->entry->kind == scripting::PluginEntryKind::Panel) {
          persistent = resolved->entry->panelPersistent;
        }
        std::string name = panel_catalog::describe(panelId).title;
        if (name.empty()) {
          name = panelId;
        }
        const std::string id = "panel:" + panelId;
        addSurface(
            id, std::move(name), "panel", pluginId, entryId, panelId, persistent, supportFor("panel", pluginId, id)
        );
      }
    }

    // Every visual entry of an enabled plugin, including entries not yet placed.
    for (const scripting::PluginEntryKind kind :
         {scripting::PluginEntryKind::Widget, scripting::PluginEntryKind::DesktopWidget}) {
      const bool desktop = kind == scripting::PluginEntryKind::DesktopWidget;
      const std::string entryKind = desktop ? "plugin-desktop-widget" : "plugin-widget";
      for (const scripting::ResolvedPluginEntry& resolved : registry.entriesOfKind(kind)) {
        const std::string fullId = resolved.fullId();
        const std::string id = entryKind + ":" + fullId;
        addSurface(
            id, pluginEntryLabel(resolved), entryKind, resolved.manifest->id, resolved.entry->id, fullId, false,
            supportFor(entryKind, resolved.manifest->id, id)
        );
      }
    }

    // Headless entries: services, shortcuts, and launcher providers own no screen
    // and never get an image; they are listed as text metadata only.
    const auto addNonvisual = [&](const std::string& kind, const scripting::ResolvedPluginEntry& resolved) {
      const std::string id = resolved.fullId();
      if (!validIdentity(id) || !validIdentity(resolved.manifest->id) || !validIdentity(resolved.entry->id)) {
        complete = false;
        return;
      }
      if (nonvisual.contains(id)) {
        return;
      }
      if (nonvisual.size() >= kMaxCatalogNonvisual) {
        complete = false;
        return;
      }
      nonvisual.emplace(
          id,
          nlohmann::json{
              {"id", id},
              {"name", displayName(pluginEntryLabel(resolved))},
              {"kind", kind},
              {"plugin_id", boundedUtf8(resolved.manifest->id, kMaxSurfaceTextBytes)},
              {"entry_id", boundedUtf8(resolved.entry->id, kMaxSurfaceTextBytes)},
          }
      );
    };
    for (const scripting::ResolvedPluginEntry& resolved : registry.entriesOfKind(scripting::PluginEntryKind::Service)) {
      addNonvisual("service", resolved);
    }
    for (const scripting::ResolvedPluginEntry& resolved :
         registry.entriesOfKind(scripting::PluginEntryKind::Shortcut)) {
      addNonvisual("shortcut", resolved);
    }
    for (const scripting::ResolvedPluginEntry& resolved :
         registry.entriesOfKind(scripting::PluginEntryKind::LauncherProvider)) {
      addNonvisual("launcher-provider", resolved);
    }

    // A bounded partial list must never be advertised as complete coverage.
    // Older consumers can still read the rest of skin-status; whole-skin work
    // fails closed when this optional catalog is unavailable.
    if (!complete || surfaces.empty()) {
      return nullptr;
    }

    nlohmann::json surfaceList = nlohmann::json::array();
    for (auto& entry : surfaces) {
      surfaceList.push_back(std::move(entry.second));
    }
    nlohmann::json nonvisualList = nlohmann::json::array();
    for (auto& entry : nonvisual) {
      nonvisualList.push_back(std::move(entry.second));
    }
    return nlohmann::json{
        {"schema_version", 1},
        {"surfaces", std::move(surfaceList)},
        {"nonvisual", std::move(nonvisualList)},
    };
  }
  // --- process-only presentation profile --------------------------------------

} // namespace

namespace noctalia::shell::skins {

  void SkinService::applyPresentationProfile(const std::optional<SkinRecipe>& recipe) {
    const bool stagecraft = recipe.has_value() && recipe->family == "stagecraft";
    const bool kumihimo = recipe.has_value() && recipe->family == "kumihimo";
    Style::setStagecraftEnabled(stagecraft);
    Style::setKumihimoEnabled(kumihimo);
    if (!kumihimo) {
      setPresentationPalette(std::nullopt);
      return;
    }
    // Inert, validated recipe colors are presentation only. ThemeService continues
    // updating the underlying palette; no templates, GSettings or config writes.
    Palette material = basePalette();
    material.surface = hex(recipe->colorSurface);
    material.surfaceVariant = hex(recipe->colorSurfaceVariant);
    material.onSurface = hex(recipe->colorText);
    material.onSurfaceVariant = hex(recipe->colorMuted);
    material.primary = hex(recipe->colorAccent);
    material.onPrimary = readableTextColorForBackground(material.primary);
    material.secondary = material.onSurfaceVariant;
    material.onSecondary = readableTextColorForBackground(material.secondary);
    material.outline = hex(recipe->colorOutline);
    material.hover = lerpColor(material.surfaceVariant, material.primary, 0.12F);
    material.onHover = material.onSurface;
    setPresentationPalette(material);
  }

} // namespace noctalia::shell::skins

namespace noctalia::shell::skins {

  SkinService::SkinService(ConfigService& config, PanelManager& panels) : m_config(config), m_panels(panels) {}

  SkinService::~SkinService() = default;

  bool SkinService::isActiveProfileValidated() const { return activeRecipe().has_value(); }

  void SkinService::setProfileReconcileCallback(ProfileReconcileCallback callback) {
    m_profileReconcile = std::move(callback);
  }

  void SkinService::maskScriptSnapshot(toml::table& snapshot) const {
    if (isActiveProfileValidated()) {
      return;
    }
    // The persisted override is left exactly as it is; only the script-visible copy of
    // the config loses the marker, so plugin gates fall back to their inactive default.
    auto* pluginSettings = snapshot.get("plugin_settings");
    if (pluginSettings == nullptr || !pluginSettings->is_table()) {
      return;
    }
    auto* perPlugin = pluginSettings->as_table()->get(std::string(kPluginId));
    if (perPlugin == nullptr || !perPlugin->is_table()) {
      return;
    }
    perPlugin->as_table()->erase("applied_variant");
  }

  std::string SkinService::statusJson() const {
    nlohmann::json status = nlohmann::json::object();
    status["schema_version"] = 1;
    nlohmann::json families = nlohmann::json::array();
    for (const std::string_view family : kSupportedFamilies) {
      families.push_back(std::string(family));
    }
    status["supported_families"] = std::move(families);

    const auto active = activeRecipe();
    status["active"] = active.has_value() ? recipeJson(*active) : nlohmann::json(nullptr);

    const std::string wallpaperPath = m_config.getPaletteWallpaperPath();
    status["wallpaper_path"] = wallpaperPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(wallpaperPath);

    const auto music = musicSummary();
    status["music"] = music.has_value() ? *music : nlohmann::json(nullptr);
    const auto ai = aiSummary();
    status["ai"] = ai.has_value() ? *ai : nlohmann::json(nullptr);

    status["persistent_music_open"] = m_panels.isOpenPanel(kMusicPanelId);
    // Additive, bounded inventory of the shell surfaces a whole-skin package could
    // cover. Built here only, so the hot layout path pays nothing.
    status["surface_catalog"] = surfaceCatalog(m_config, m_panels, active);
    return status.dump() + "\n";
  }

  std::string SkinService::applyJson(std::string_view recipeJsonText) {
    std::string error;
    const auto parsed = parseRecipe(recipeJsonText, &error);
    if (!parsed.has_value()) {
      return refusalJson(error);
    }
    const SkinRecipe& recipe = *parsed;
    const std::vector<std::pair<std::string, std::string>> projection = projectionOf(recipe);

    // Read the previous receipt before touching anything: it is the only record of what
    // the last apply wrote and what it replaced. Reapply must not re-capture the skin's
    // own values as "originals".
    const auto previousReceipt = readReceipt();

    // The Kumihimo bar name is reserved: a bar the user already configured under it is
    // never adopted. Refuse before any mutation unless this very skin already owns it (a
    // re-apply or a legacy receipt), so a user's own bar is never silently rewritten.
    if (recipe.family == "kumihimo") {
      const bool alreadyOwned =
          previousReceipt.has_value() && previousReceipt->barOwned && previousReceipt->barName == kStatusBarName;
      if (!alreadyOwned && findBar(m_config.config(), kStatusBarName) != nullptr) {
        return refusalJson("the reserved bar \"" + std::string(kStatusBarName) + "\" is already configured");
      }
      // The dedicated tray instance id is reserved the same way: a `[widget.*]` table the
      // user configured under it is never adopted or rewritten. A table this skin wrote
      // (recorded owned in the previous receipt) is fine to rewrite.
      const bool trayAlreadyOwned =
          previousReceipt.has_value() && previousReceipt->trayOwned && previousReceipt->trayId == kTrayInstanceId;
      if (!trayAlreadyOwned && m_config.config().widgets.contains(std::string(kTrayInstanceId))) {
        return refusalJson(
            "the reserved widget instance \"" + std::string(kTrayInstanceId) + "\" is already configured"
        );
      }
    }

    // A recipe is only meaningful if the presentation plugin will actually consume it.
    // Register/enable exactly what is missing; a failed call reverts its own additions
    // and refuses rather than leave a consumer registered behind.
    const PluginRegistration registration = ensurePresentationPlugin();
    if (!registration.ok) {
      return refusalJson(registration.error);
    }

    // Capture the user layer before writing: these are the values a later clear puts
    // back, and the ownership record that makes "cleared" mean "exactly what I wrote".
    const auto before = userLayerSettings();
    const std::vector<std::pair<std::string, std::string>> previousProjection = previousReceipt.has_value()
        ? projectionOf(previousReceipt->recipe)
        : std::vector<std::pair<std::string, std::string>>{};

    std::vector<OwnedSetting> originals;
    originals.reserve(projection.size());
    std::vector<std::string> stalePluginKeys;
    for (const auto& [key, value] : projection) {
      (void)value;
      const std::optional<std::string> previousValue = findSetting(previousProjection, key);
      const auto current = effectiveSetting(key);
      // A key still carrying the previous recipe's value is still ours: carry the
      // original the previous receipt recorded instead of re-capturing our own write.
      if (previousReceipt.has_value()
          && previousValue.has_value()
          && current.has_value()
          && *current == *previousValue) {
        const OwnedSetting* previousOriginal = findOwned(previousReceipt->originals, key);
        originals.push_back(previousOriginal != nullptr ? *previousOriginal : OwnedSetting{.key = key});
        continue;
      }
      // No previous claim on the key, or the user edited it since: whatever the user
      // layer holds right now becomes the value a clear restores.
      OwnedSetting original;
      original.key = key;
      const auto layerValue = before.has_value() ? findSetting(*before, key) : std::nullopt;
      original.hadOverride = layerValue.has_value() && m_config.hasOverride(overridePath(key));
      if (original.hadOverride) {
        original.originalValue = *layerValue;
      }
      originals.push_back(std::move(original));
    }
    // Keys the previous recipe published and this one does not stop being ours.
    for (const auto& [key, value] : previousProjection) {
      if (findSetting(projection, key).has_value()) {
        continue;
      }
      const auto current = effectiveSetting(key);
      if (current.has_value() && *current == value) {
        stalePluginKeys.push_back(key);
      }
    }

    // Kumihimo owns a real top bar; every other family still creates none. First collect
    // the retirement of any bar the previous receipt owned — dropped whole only while
    // every field the receipt proves this skin wrote still matches, user-edited bars and
    // foreign fields preserved — then plan the fresh install. Clears precede sets in the
    // one commit, so a shape change (or a Stagecraft-era legacy bar becoming the Kumihimo
    // one) rewrites the bar cleanly instead of leaving stale fields behind.
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> legacySets;
    std::vector<std::vector<std::string>> legacyClears;
    bool legacyDropCarrier = false;
    const bool ownsBar = recipe.family == "kumihimo";
    if (previousReceipt.has_value()) {
      collectLegacyBarCleanup(m_config, *previousReceipt, legacySets, legacyClears, legacyDropCarrier);
      // The previous receipt's tray instance and default-bar disable follow the same
      // ownership rules as its bar. On a Kumihimo reapply the fresh install below
      // rewrites both, so their cleanup still precedes the sets in this one commit; on a
      // family switch nothing rewrites them and the restore must happen here.
      collectTrayCleanup(m_config, *previousReceipt, legacySets, legacyClears);
      if (!ownsBar) {
        collectDefaultBarDisableRestore(m_config, *previousReceipt, legacySets, legacyClears);
      }
    }
    OwnedBarInstall barInstall;
    if (ownsBar) {
      barInstall = planOwnedBarInstall(m_config, recipe, previousReceipt);
    }
    // Capture the immediate pre-apply state of every legacy/carrier path the commit
    // touches, so a later receipt failure restores the bars too — the plugin rollback
    // below alone would leave generated-bar removals committed.
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> rollbackSets;
    std::vector<std::vector<std::string>> rollbackClears;
    const auto captureRollbackFor = [&](const std::vector<std::string>& path) {
      if (path.size() >= 2 && path[0] == "widget") {
        captureTrayRollback(m_config, path, rollbackSets, rollbackClears);
      } else {
        captureLegacyBarRollback(m_config, path, rollbackSets, rollbackClears);
      }
    };
    for (const auto& path : legacyClears) {
      captureRollbackFor(path);
    }
    for (const auto& [path, value] : legacySets) {
      (void)value;
      captureRollbackFor(path);
    }
    for (const auto& [path, value] : barInstall.sets) {
      (void)value;
      captureRollbackFor(path);
    }
    for (const auto& [path, value] : barInstall.traySets) {
      (void)value;
      captureTrayRollback(m_config, path, rollbackSets, rollbackClears);
    }
    if (barInstall.disablesDefaultBar) {
      captureLegacyBarRollback(
          m_config, {"bar", std::string(kImplicitDefaultBar), "enabled"}, rollbackSets, rollbackClears
      );
    }
    if (legacyDropCarrier) {
      captureLegacyBarRollback(
          m_config, {"bar", std::string(kImplicitDefaultBar), "enabled"}, rollbackSets, rollbackClears
      );
    }

    // The immediate pre-apply override state of the plugin keys, captured per touched
    // path, so a receipt failure rolls back to exactly what was there a moment ago —
    // never to pre-skin originals, which would silently unapply the previous recipe.
    const auto capturePluginRollback = [&](std::string_view key) {
      const std::vector<std::string> path = overridePath(key);
      const auto layerValue = before.has_value() ? findSetting(*before, key) : std::nullopt;
      if (m_config.hasOverride(path) && layerValue.has_value()) {
        rollbackSets.emplace_back(path, ConfigOverrideValue{*layerValue});
      } else {
        rollbackClears.push_back(path);
      }
    };
    for (const auto& [key, value] : projection) {
      (void)value;
      capturePluginRollback(key);
    }
    for (const auto& key : stalePluginKeys) {
      capturePluginRollback(key);
    }

    // One atomic commit: plugin settings, retired keys, and the owned bar (plus — when a
    // legacy receipt still owns one — its migration) move together, so config never shows
    // a half skin.
    std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> sets;
    sets.reserve(projection.size() + legacySets.size() + barInstall.sets.size() + barInstall.traySets.size());
    for (const auto& [key, value] : projection) {
      sets.emplace_back(overridePath(key), ConfigOverrideValue{value});
    }
    for (auto& entry : legacySets) {
      sets.push_back(std::move(entry));
    }
    for (auto& entry : barInstall.sets) {
      sets.push_back(std::move(entry));
    }
    for (auto& entry : barInstall.traySets) {
      sets.push_back(std::move(entry));
    }
    std::vector<std::vector<std::string>> clears;
    clears.reserve(stalePluginKeys.size() + legacyClears.size() + 1);
    for (const auto& key : stalePluginKeys) {
      clears.push_back(overridePath(key));
    }
    for (auto& path : legacyClears) {
      clears.push_back(std::move(path));
    }
    collectDefaultCarrierCleanup(m_config, legacyDropCarrier, clears);

    if (!commitOverrides(m_config, sets, clears, &error)) {
      revertPluginRegistration(registration.addedSource, registration.enabledPlugin);
      return refusalJson(error);
    }

    // Registration is receipt-scoped and sticky across re-applies: once the skin added a
    // source or enabled the plugin, that ownership survives a later apply of another
    // recipe so a clear still reverts exactly it. Bar ownership is Kumihimo-only: a fresh
    // Kumihimo receipt carries the owned bar section, every other family carries none —
    // and any bar an earlier receipt owned was just migrated away above.
    SkinReceipt receipt;
    receipt.recipe = recipe;
    receipt.originals = originals;
    receipt.addedSource = registration.addedSource || (previousReceipt.has_value() && previousReceipt->addedSource);
    receipt.enabledPlugin =
        registration.enabledPlugin || (previousReceipt.has_value() && previousReceipt->enabledPlugin);
    receipt.barOwned = ownsBar;
    if (ownsBar) {
      receipt.barName = std::string(kStatusBarName);
      // Independent AI opener after monitoring; centered music remains unchanged.
      receipt.barGeometryRevision = kKumihimoBarGeometryRevisionAiCassette;
      receipt.barCarriedDefault = barInstall.carriedDefault;
      receipt.barOriginals = std::move(barInstall.originals);
      receipt.trayOwned = true;
      receipt.trayId = std::string(kTrayInstanceId);
      receipt.trayOriginals = std::move(barInstall.trayOriginals);
      receipt.defaultBarDisabled = barInstall.disablesDefaultBar;
      receipt.defaultBarEnabledOriginal = barInstall.defaultBarEnabledOriginal;
    }

    // The receipt is the commit's second half: if it cannot be stored, undo the config
    // mutation so status, config, and receipt never disagree about what is applied.
    if (!m_config.setStateString(kStateOwner, kReceiptKey, receiptJson(receipt))) {
      std::string rollbackError;
      if (!commitOverrides(m_config, rollbackSets, rollbackClears, &rollbackError)) {
        revertPluginRegistration(registration.addedSource, registration.enabledPlugin);
        kLog.warn("skin receipt failed and the overrides could not be rolled back: {}", rollbackError);
        return refusalJson(
            "failed to persist the skin receipt; the overrides could not be rolled back: " + rollbackError
        );
      }
      revertPluginRegistration(registration.addedSource, registration.enabledPlugin);
      return refusalJson("failed to persist the skin receipt; overrides rolled back");
    }

    reconcilePersistentPanels();
    return successJson(recipe);
  }

  std::string SkinService::clearJson() {
    const auto receipt = readReceipt();
    if (receipt.has_value()) {
      std::string error;
      std::vector<std::pair<std::vector<std::string>, ConfigOverrideValue>> sets;
      std::vector<std::vector<std::string>> clears;
      for (const auto& [key, value] : projectionOf(receipt->recipe)) {
        const auto current = effectiveSetting(key);
        // Only values still exactly as the skin wrote them are touched: an intervening
        // edit through any layer is left alone.
        if (!current.has_value() || *current != value) {
          continue;
        }
        const OwnedSetting* original = findOwned(receipt->originals, key);
        if (original != nullptr && original->hadOverride) {
          sets.emplace_back(overridePath(key), ConfigOverrideValue{original->originalValue});
        } else if (m_config.hasOverride(overridePath(key))) {
          clears.push_back(overridePath(key));
        }
      }

      // A receipt from an earlier version may still own the generated
      // `skin-runtime-status` bar: drop it whole only while every projected field still
      // matches and the bar carries nothing the skin did not write; otherwise restore
      // the fields that are still ours and leave the edited bar — and its widget — in
      // place. Its implicit-default carrier goes with it in the same commit.
      bool dropDefaultCarrier = false;
      collectLegacyBarCleanup(m_config, *receipt, sets, clears, dropDefaultCarrier);
      collectDefaultCarrierCleanup(m_config, dropDefaultCarrier, clears);
      collectTrayCleanup(m_config, *receipt, sets, clears);
      collectDefaultBarDisableRestore(m_config, *receipt, sets, clears);

      if (!commitOverrides(m_config, sets, clears, &error)) {
        return refusalJson(error);
      }
    }

    // Revert only the registration this skin performed; anything the user already had
    // (another source covering the root, a pre-existing enabled entry) is left alone. A
    // bar the user kept after editing still hosts the plugin's widget, so its
    // registration must survive the clear.
    if (receipt.has_value() && !presentationWidgetReferenced(m_config.config())) {
      revertPluginRegistration(receipt->addedSource, receipt->enabledPlugin);
    }

    // The receipt is inert once the published values no longer match, so dropping it is
    // always safe even if the override restore above was a no-op.
    if (!m_config.clearStateOwner(kStateOwner)) {
      kLog.warn("failed to clear the skin receipt");
    }

    reconcilePersistentPanels();
    return nlohmann::json{{"ok", true}, {"active", nullptr}}.dump() + "\n";
  }

  void SkinService::reconcilePersistentPanels() {
    // The process-only visual profile follows the same active-receipt source of truth as
    // the panels: applied after a successful commit, reconciled from the receipt on
    // startup, config reload, and plugin enable/disable, and dropped on clear. Never
    // called from a frame path, so the overlay is written once per state change, not per
    // frame.
    applyPresentationProfile(activeRecipe());

    const auto active = activeRecipe();
    const bool wantOpen = active.has_value() && active->family == "stagecraft";

    // The two passive scenes this service owns: the music stage and the desktop status
    // panel. Nothing else is opened or closed here, so a normal panel the user has open
    // is untouched.
    constexpr std::array<std::string_view, 2> kOwnedPanels{kMusicPanelId, kStatusPanelId};
    const std::vector<std::string> available = m_panels.availablePanelIds();
    for (const std::string_view panelIdView : kOwnedPanels) {
      const std::string panelId(panelIdView);
      const bool registered = std::ranges::contains(available, panelId);
      if (wantOpen) {
        // Idempotent: a panel that is already open is left untouched, so a reload storm
        // can never toggle it closed and open again. An owned panel whose plugin entry is
        // not shipped yet is skipped rather than attempted.
        if (registered && !m_panels.isOpenPanel(panelId)) {
          m_panels.openPanel(panelId);
        }
      } else if (m_panels.isOpenPanel(panelId)) {
        // Close only these owned panels; other panels and the desktop widgets stay
        // untouched.
        m_panels.closePanelById(panelId);
      }
    }

    // The receipt is the single source of truth for the masked script snapshot: publish it
    // only once the process-only profile and the owned panels above have followed it, so
    // every startup/reload/plugin change/apply/clear caller republishes the same reconciled
    // marker. The callback only publishes the snapshot and never re-enters this method, so
    // firing it here cannot recurse.
    if (m_profileReconcile) {
      m_profileReconcile();
    }
  }

  SkinService::PluginRegistration SkinService::ensurePresentationPlugin() {
    PluginRegistration result;
    if (pluginLoaded(kPluginId)) {
      result.ok = true;
      return result;
    }

    // Installed but not discoverable is the only case this can fix: without the plugin
    // on disk the recipe has no consumer and the apply must fail closed.
    const std::string root = packagedPluginsRoot();
    const std::filesystem::path packagedPlugin = std::filesystem::path(root) / "skin-runtime";
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(packagedPlugin / "plugin.toml", ec)) {
      result.error = "the q/skin-runtime plugin is not installed";
      return result;
    }

    bool nameTaken = false;
    for (const auto& source : m_config.config().plugins.sources) {
      if (source.name == kPresentationSourceName) {
        nameTaken = true;
        break;
      }
    }

    // Reuse a source that already covers the packaged root; otherwise append ours. The
    // source list is appended to (never replaced), so the user's own sources survive.
    if (!registeredSourceCovers(root) && !nameTaken) {
      m_config.addPluginSource(
          PluginSourceConfig{
              .kind = PluginSourceKind::Path,
              .name = std::string(kPresentationSourceName),
              .location = root,
          }
      );
      result.addedSource = true;
    }

    // Opt-in registry: the plugin loads only when its id is in [plugins].enabled, and
    // the enabled list is appended to, preserving every other entry.
    bool alreadyEnabled = false;
    for (const auto& id : m_config.config().plugins.enabled) {
      if (id == kPluginId) {
        alreadyEnabled = true;
        break;
      }
    }
    if (!alreadyEnabled) {
      m_config.setPluginEnabled(kPluginId, true);
      result.enabledPlugin = true;
    }

    if (!pluginLoaded(kPluginId)) {
      if (nameTaken) {
        result.error = "the plugin source name '"
            + std::string(kPresentationSourceName)
            + "' is already used and does not provide q/skin-runtime";
      } else {
        result.error = "the q/skin-runtime plugin could not be loaded";
      }
      // This call may already have appended a source and enabled the id; undo exactly
      // that so a failed apply never leaves a registered consumer behind.
      revertPluginRegistration(result.addedSource, result.enabledPlugin);
      return result;
    }
    result.ok = true;
    return result;
  }

  void SkinService::revertPluginRegistration(bool addedSource, bool enabledPlugin) {
    if (enabledPlugin) {
      m_config.setPluginEnabled(kPluginId, false);
    }
    if (addedSource && !otherPluginDependsOnSource(packagedPluginsRoot())) {
      m_config.removePluginSource(kPresentationSourceName);
    }
  }

  bool SkinService::registeredSourceCovers(const std::string& root) const {
    if (root.empty()) {
      return false;
    }
    const std::filesystem::path pluginDir = std::filesystem::path(root) / "skin-runtime";
    for (const auto& source : m_config.config().plugins.sources) {
      if (!source.enabled) {
        continue;
      }
      const std::filesystem::path resolved = scripting::plugin_paths::registryRoot(source);
      if (resolved.empty()) {
        continue;
      }
      if (resolved.lexically_normal() == std::filesystem::path(root).lexically_normal()
          || scripting::plugin_paths::pathIsInside(pluginDir, resolved)) {
        return true;
      }
    }
    return false;
  }

  bool SkinService::otherPluginDependsOnSource(const std::string& root) const {
    if (root.empty()) {
      return false;
    }
    auto& registry = scripting::PluginRegistry::instance();
    registry.ensureScanned();
    for (const auto& id : m_config.config().plugins.enabled) {
      if (id == kPluginId) {
        continue;
      }
      const auto dir = registry.findPluginDir(id);
      if (dir.has_value() && scripting::plugin_paths::pathIsInside(*dir, std::filesystem::path(root))) {
        return true;
      }
    }
    return false;
  }

  std::optional<SkinService::SettingList> SkinService::userLayerSettings() const {
    const std::string merged = m_config.buildMergedUserConfig();
    toml::table root;
    try {
      root = toml::parse(merged);
    } catch (const toml::parse_error& error) {
      kLog.warn("cannot read the user config layer: {}", error.description());
      return std::nullopt;
    }

    SettingList settings;
    const auto* pluginSettings = root["plugin_settings"].as_table();
    if (pluginSettings == nullptr) {
      return settings;
    }
    const auto* perPlugin = (*pluginSettings)[std::string(kPluginId)].as_table();
    if (perPlugin == nullptr) {
      return settings;
    }
    for (const auto& [key, node] : *perPlugin) {
      if (const auto value = node.value<std::string>()) {
        settings.emplace_back(std::string(key.str()), *value);
      }
    }
    return settings;
  }

  std::optional<std::string> SkinService::effectiveSetting(std::string_view key) const {
    const auto& pluginSettings = m_config.config().plugins.pluginSettings;
    if (const auto pluginIt = pluginSettings.find(std::string(kPluginId)); pluginIt != pluginSettings.end()) {
      if (const auto valueIt = pluginIt->second.find(std::string(key)); valueIt != pluginIt->second.end()) {
        if (const auto* value = std::get_if<std::string>(&valueIt->second)) {
          return *value;
        }
      }
    }

    // No configured value: resolve the manifest default the way the plugin host seeds a
    // runtime — plugin-level settings first, then each panel entry's settings, which
    // carry the injected `<entry>_position` host setting. That keeps a redundant override
    // (one the writer dropped because it matched its default) counted as applied instead
    // of silently inactive.
    auto& registry = scripting::PluginRegistry::instance();
    registry.ensureScanned();
    const scripting::PluginManifest* manifest = registry.findManifest(kPluginId);
    if (manifest == nullptr) {
      return std::nullopt;
    }
    const auto fieldDefault = [key](const std::vector<scripting::ManifestField>& fields) -> std::optional<std::string> {
      for (const auto& field : fields) {
        if (field.key != key) {
          continue;
        }
        const WidgetSettingValue value = field.defaultValue();
        if (const auto* stringValue = std::get_if<std::string>(&value)) {
          return *stringValue;
        }
      }
      return std::nullopt;
    };
    if (const auto value = fieldDefault(manifest->settings); value.has_value()) {
      return value;
    }
    for (const auto& entry : manifest->entries) {
      if (entry.kind != scripting::PluginEntryKind::Panel) {
        continue;
      }
      if (const auto value = fieldDefault(entry.settings); value.has_value()) {
        return value;
      }
    }
    return std::nullopt;
  }

  std::optional<SkinReceipt> SkinService::readReceipt() const {
    const auto raw = m_config.stateString(kStateOwner, kReceiptKey);
    if (!raw.has_value()) {
      return std::nullopt;
    }
    const nlohmann::json doc = nlohmann::json::parse(*raw, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
      return std::nullopt;
    }
    if (const auto versionIt = doc.find("schema_version");
        versionIt == doc.end() || !versionIt->is_number_integer() || versionIt->get<std::int64_t>() != 1) {
      return std::nullopt;
    }
    const auto recipeIt = doc.find("recipe");
    if (recipeIt == doc.end() || !recipeIt->is_object()) {
      return std::nullopt;
    }
    std::string error;
    auto recipe = parseRecipe(recipeIt->dump(), &error);
    if (!recipe.has_value()) {
      kLog.warn("ignoring unreadable skin receipt: {}", error);
      return std::nullopt;
    }

    SkinReceipt receipt;
    receipt.recipe = std::move(*recipe);
    if (const auto originalsIt = doc.find("originals"); originalsIt != doc.end() && originalsIt->is_array()) {
      for (const auto& entry : *originalsIt) {
        if (!entry.is_object()) {
          continue;
        }
        const auto keyIt = entry.find("key");
        if (keyIt == entry.end() || !keyIt->is_string() || keyIt->get<std::string>().empty()) {
          continue;
        }
        OwnedSetting original;
        original.key = keyIt->get<std::string>();
        if (const auto hadOverrideIt = entry.find("had_override");
            hadOverrideIt != entry.end() && hadOverrideIt->is_boolean()) {
          original.hadOverride = hadOverrideIt->get<bool>();
        }
        if (const auto valueIt = entry.find("original_value"); valueIt != entry.end() && valueIt->is_string()) {
          original.originalValue = valueIt->get<std::string>();
        }
        receipt.originals.push_back(std::move(original));
      }
    }
    if (const auto pluginIt = doc.find("plugin"); pluginIt != doc.end() && pluginIt->is_object()) {
      if (const auto addedSourceIt = pluginIt->find("added_source");
          addedSourceIt != pluginIt->end() && addedSourceIt->is_boolean()) {
        receipt.addedSource = addedSourceIt->get<bool>();
      }
      if (const auto enabledIt = pluginIt->find("enabled_plugin");
          enabledIt != pluginIt->end() && enabledIt->is_boolean()) {
        receipt.enabledPlugin = enabledIt->get<bool>();
      }
    }
    if (const auto barIt = doc.find("bar"); barIt != doc.end() && barIt->is_object()) {
      // Ownership is only accepted for the one fixed reserved name: a receipt can never
      // point the restore at an arbitrary bar.
      const auto nameIt = barIt->find("name");
      const auto ownedIt = barIt->find("owned");
      if (ownedIt != barIt->end()
          && ownedIt->is_boolean()
          && ownedIt->get<bool>()
          && nameIt != barIt->end()
          && nameIt->is_string()
          && nameIt->get<std::string>() == kStatusBarName) {
        receipt.barOwned = true;
        receipt.barName = std::string(kStatusBarName);
      }
      if (const auto carriedIt = barIt->find("carried_default");
          receipt.barOwned && carriedIt != barIt->end() && carriedIt->is_boolean()) {
        receipt.barCarriedDefault = carriedIt->get<bool>();
      }
      // Keep every supported historical shape valid until its native reapply.
      if (const auto revisionIt = barIt->find("geometry_revision"); receipt.barOwned
          && revisionIt != barIt->end()
          && revisionIt->is_number_integer()
          && (revisionIt->get<std::int64_t>() == kKumihimoBarGeometryRevisionCompact
              || revisionIt->get<std::int64_t>() == kKumihimoBarGeometryRevisionMusic
              || revisionIt->get<std::int64_t>() == kKumihimoBarGeometryRevisionJoinery
              || revisionIt->get<std::int64_t>() == kKumihimoBarGeometryRevisionAiCassette)) {
        receipt.barGeometryRevision = revisionIt->get<std::int32_t>();
      }
      if (receipt.barOwned) {
        if (const auto fieldsIt = barIt->find("fields"); fieldsIt != barIt->end() && fieldsIt->is_array()) {
          for (const auto& entry : *fieldsIt) {
            if (!entry.is_object()) {
              continue;
            }
            const auto fieldIt = entry.find("field");
            if (fieldIt == entry.end() || !fieldIt->is_string() || fieldIt->get<std::string>().empty()) {
              continue;
            }
            OwnedSetting original;
            original.key = fieldIt->get<std::string>();
            if (const auto hadOverrideIt = entry.find("had_override");
                hadOverrideIt != entry.end() && hadOverrideIt->is_boolean()) {
              original.hadOverride = hadOverrideIt->get<bool>();
            }
            if (const auto valueIt = entry.find("value"); valueIt != entry.end() && !valueIt->is_null()) {
              original.originalValue = valueIt->dump();
            }
            receipt.barOriginals.push_back(std::move(original));
          }
        }
      }
    }
    // Dedicated tray instance: ownership is only accepted for the one fixed reserved id,
    // so a receipt can never point the restore at an arbitrary widget table.
    if (const auto trayIt = doc.find("tray"); trayIt != doc.end() && trayIt->is_object()) {
      const auto ownedIt = trayIt->find("owned");
      const auto idIt = trayIt->find("id");
      if (ownedIt != trayIt->end()
          && ownedIt->is_boolean()
          && ownedIt->get<bool>()
          && idIt != trayIt->end()
          && idIt->is_string()
          && idIt->get<std::string>() == kTrayInstanceId) {
        receipt.trayOwned = true;
        receipt.trayId = std::string(kTrayInstanceId);
        if (const auto fieldsIt = trayIt->find("fields"); fieldsIt != trayIt->end() && fieldsIt->is_array()) {
          // Only fields the tray projection actually writes are accepted, each once:
          // the tray receipt path stays bounded to the closed projection instead of
          // becoming a general restore-anything channel.
          const std::vector<BarFieldSpec> trayProjection = trayProjectionOf(receipt.recipe);
          std::unordered_set<std::string> acceptedKeys;
          for (const auto& spec : trayProjection) {
            acceptedKeys.insert(spec.field);
          }
          for (const auto& entry : *fieldsIt) {
            if (!entry.is_object()) {
              continue;
            }
            const auto fieldIt = entry.find("field");
            if (fieldIt == entry.end() || !fieldIt->is_string() || fieldIt->get<std::string>().empty()) {
              continue;
            }
            OwnedSetting original;
            original.key = fieldIt->get<std::string>();
            if (acceptedKeys.erase(original.key) == 0) {
              continue; // unknown or duplicate field: keep the first
            }
            if (const auto hadOverrideIt = entry.find("had_override");
                hadOverrideIt != entry.end() && hadOverrideIt->is_boolean()) {
              original.hadOverride = hadOverrideIt->get<bool>();
            }
            if (const auto valueIt = entry.find("value"); valueIt != entry.end() && !valueIt->is_null()) {
              original.originalValue = valueIt->dump();
            }
            receipt.trayOriginals.push_back(std::move(original));
          }
        }
      }
    }
    // Only an owned Kumihimo receipt with a valid boolean original may claim
    // the default-bar disable. Missing undo data never acquires ownership.
    if (const auto defaultIt = doc.find("default_bar");
        receipt.barOwned && receipt.recipe.family == "kumihimo" && defaultIt != doc.end() && defaultIt->is_object()) {
      const auto disabledIt = defaultIt->find("disabled");
      const auto originalIt = defaultIt->find("enabled_original");
      if (disabledIt != defaultIt->end()
          && disabledIt->is_boolean()
          && disabledIt->get<bool>()
          && originalIt != defaultIt->end()
          && originalIt->is_object()) {
        const auto hadOverrideIt = originalIt->find("had_override");
        const auto valueIt = originalIt->find("value");
        if (hadOverrideIt != originalIt->end()
            && hadOverrideIt->is_boolean()
            && (!hadOverrideIt->get<bool>() || (valueIt != originalIt->end() && valueIt->is_boolean()))) {
          receipt.defaultBarDisabled = true;
          receipt.defaultBarEnabledOriginal.key = "enabled";
          receipt.defaultBarEnabledOriginal.hadOverride = hadOverrideIt->get<bool>();
          if (receipt.defaultBarEnabledOriginal.hadOverride) {
            receipt.defaultBarEnabledOriginal.originalValue = valueIt->dump();
          }
        }
      }
    }
    return receipt;
  }

  std::optional<SkinRecipe> SkinService::activeRecipe() const {
    const auto receipt = readReceipt();
    if (!receipt.has_value()) {
      return std::nullopt;
    }
    // A recipe is active only while every published plugin value is still exactly in
    // effect. A manual edit anywhere drops the skin out of the active state instead of
    // letting status advertise a candidate that is no longer applied.
    for (const auto& [key, value] : projectionOf(receipt->recipe)) {
      const auto current = effectiveSetting(key);
      if (!current.has_value() || *current != value) {
        return std::nullopt;
      }
    }
    // A legacy receipt (one from a version that created the generated status bar) is
    // only active while that bar exists and its projection is still what the skin wrote.
    // Fresh receipts carry no bar: the user's own bars are not part of the applied
    // state, so plugin settings alone decide activation.
    if (!receipt->barOwned || receipt->barName != kStatusBarName) {
      return receipt->recipe;
    }
    const BarConfig* bar = findBar(m_config.config(), kStatusBarName);
    if (bar == nullptr) {
      return std::nullopt;
    }
    // A pre-correction Kumihimo receipt is validated against the shape it actually wrote,
    // so it stays active until an authorized reapply migrates it.
    for (const auto& spec : receiptBarProjectionOf(*receipt)) {
      const auto current = effectiveBarFieldValue(*bar, spec.field);
      if (!current.has_value() || !overrideValueEquals(*current, spec.value)) {
        return std::nullopt;
      }
    }
    // The dedicated tray instance is part of the applied state the same way: a manual
    // edit of the reserved table drops the skin out of the active state.
    if (receipt->trayOwned && receipt->trayId == kTrayInstanceId) {
      for (const auto& spec : receiptTrayProjectionOf(*receipt)) {
        const auto current = effectiveTrayFieldValue(m_config.config(), spec.field);
        if (!current.has_value() || !overrideValueEquals(*current, spec.value)) {
          return std::nullopt;
        }
      }
    }
    return receipt->recipe;
  }

} // namespace noctalia::shell::skins
