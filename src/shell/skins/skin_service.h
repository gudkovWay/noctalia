#pragma once

#include "core/toml.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ConfigService;
class PanelManager;

namespace noctalia::shell::skins {

  // Native mirror of the backend "recipe v1" contract. Every member is inert data:
  // the service never executes, fetches, or resolves anything a recipe names. The
  // preview path is validated as an absolute local path and then never touched.
  struct SkinRecipe {
    std::string variantId; // 32 lowercase hex
    std::string contentId; // 64 lowercase hex
    std::string name;      // 1..100 bytes, no control characters
    std::string family;    // "stagecraft" | "kumihimo" are the enabled families
    std::string topology;
    std::string density; // "compact" | "comfortable"
    std::string colorSurface;
    std::string colorSurfaceVariant;
    std::string colorText;
    std::string colorMuted;
    std::string colorAccent;
    std::string colorOutline;
    std::string previewPath; // absolute local path

    [[nodiscard]] bool operator==(const SkinRecipe&) const = default;
  };

  // One owned plugin setting or bar field plus the user-layer value that preceded the
  // skin, so a later clear can put back exactly what it replaced. For plugin settings
  // `originalValue` is the plain string the settings layer holds; for bar fields it is
  // the JSON encoding of the typed ConfigOverrideValue.
  struct OwnedSetting {
    std::string key;
    bool hadOverride = false;
    std::string originalValue;
  };

  // Receipt persisted in StateStore after a committed apply.
  struct SkinReceipt {
    SkinRecipe recipe;
    std::vector<OwnedSetting> originals;
    // Registration this skin performed, so a later clear reverts exactly that and
    // nothing the user already had.
    bool addedSource = false;
    bool enabledPlugin = false;
    // Receipt from a version that also created the dedicated status bar. Stagecraft no
    // longer creates a bar — the user's own bars keep their lanes and gain the profile
    // appearance through the common chrome path. Kumihimo does own one: a fresh Kumihimo
    // apply installs the reserved `bar.skin-runtime-status` host for its hanging tags, and
    // a clear or a family switch retires it. An old receipt's bar section is still parsed
    // and honored, so a clear or a re-apply migrates it away: the generated bar is dropped
    // only while every field this skin wrote still matches and it carries nothing else.
    bool barOwned = false;
    std::string barName;
    // Bounded geometry revision of the owned bar this receipt installed: 0 is the
    // pre-compact strip (thickness 120, padding 8, reserve_space=false, no `layer`),
    // 1 is the compact reserved top strip (thickness 64, padding 4, reserve_space=true,
    // layer=top), 2 adds the centered music entry with the same geometry.
    // Serialized inside the bar section; missing or unsupported revisions read
    // as 0, so activation/migration/clear compare the shape actually installed.
    std::int32_t barGeometryRevision = 0;
    // True when the skin materialized the implicit `default` bar so the dedicated bar's
    // creation did not delete it. Current receipts no longer carry the default bar
    // enabled: Kumihimo disables it instead. The flag survives only so a legacy
    // receipt's carrier is still parsed and retired exactly as it was written.
    bool barCarriedDefault = false;
    std::vector<OwnedSetting> barOriginals;
    // Dedicated native tray instance this skin placed at the end of its owned bar: a
    // reserved `[widget.<id>]` table of type `tray` with receipt-scoped capsule styling.
    // The global `widget.tray` entry is never written. A clear or family switch retires
    // the table by the same match-or-restore rules as the owned bar.
    bool trayOwned = false;
    std::string trayId; // always the reserved kTrayInstanceId when owned
    std::vector<OwnedSetting> trayOriginals;
    // Whether this receipt disabled the implicit default bar for Kumihimo, plus the
    // typed `enabled` original it replaced (an override's value, or nothing when the
    // bar had none). A clear restores that original only while the skin's disable is
    // still in effect; a user who re-enabled the bar keeps it.
    bool defaultBarDisabled = false;
    OwnedSetting defaultBarEnabledOriginal;
  };

  // Native skin runtime. Accepts strict data recipes, publishes them as a bounded set
  // of `plugin_settings."q/skin-runtime".*` overrides in a single atomic
  // ConfigService::mutateOverrides commit, keeps an undo receipt in StateStore, and
  // reconciles the persistent q/skin-runtime:music panel. The visual profile itself is
  // process-only: after a committed apply (and on every startup / config-reload /
  // plugin-change reconcile) the service maps the recipe onto the presentation Palette
  // and enables Stagecraft styling through the ui core, without touching the theme
  // configuration. Clear drops the overlay and restores the latest underlying palette.
  //
  // The plugin settings it owns are `plugin_settings."q/skin-runtime".*`; to make them
  // mean something it may also append — receipt-scoped, only what is missing — the
  // `[[plugins.source]]` path entry and the `[plugins].enabled` id for the presentation
  // plugin, and reverts exactly those on clear. Stagecraft creates no bar: the user's
  // configured bars and lanes stay as they are and receive the profile appearance from
  // the common chrome path. Kumihimo does own one reserved bar — the transparent top
  // `bar.skin-runtime-status` that hosts its hanging tags and the native tray — installed
  // on apply and retired on clear or family switch. Any other bar (including the user's
  // own, and a generated bar an old receipt still names) is cleaned up on apply/clear only
  // while every field the receipt proves this skin wrote still matches — a bar the user
  // edited, or any other bar, is never touched. Kumihimo also owns, receipt-scoped and by
  // the same match-or-restore rules: the reserved `q_skin_runtime_tray` widget instance of
  // the native `tray` type (never the global `widget.tray` entry), and the
  // `enabled=false` disable of the implicit default bar. Clearing restores only values this
  // service wrote and that are still untouched.
  class SkinService {
  public:
    SkinService(ConfigService& config, PanelManager& panels);
    ~SkinService();

    SkinService(const SkinService&) = delete;
    SkinService& operator=(const SkinService&) = delete;

    // `noctalia msg skin-status` payload.
    [[nodiscard]] std::string statusJson() const;
    // `noctalia msg skin-apply <JSON recipe>` payload. `recipeJson` is the raw argv
    // argument; the CLI keeps the JSON as one argument.
    [[nodiscard]] std::string applyJson(std::string_view recipeJson);
    // `noctalia msg skin-clear` payload. Restores only the owned overrides whose
    // current value is still exactly what the skin wrote.
    [[nodiscard]] std::string clearJson();

    // Whether the full validated native receipt projection is currently in effect —
    // the same source of truth reconcilePersistentPanels() and the presentation profile
    // use. Cheap enough only for lifecycle/reload/apply/clear paths, never frames.
    [[nodiscard]] bool isActiveProfileValidated() const;

    // Removes the script-visible `applied_variant` marker from a serialized config
    // snapshot (plugin_settings."q/skin-runtime".applied_variant) whenever the validated
    // native projection is NOT active, so plugin-side gates cannot disagree with the
    // native profile after a manual projected edit. Never touches persisted overrides.
    // Call on every config-snapshot publication path (initial publish and reload), and
    // republish after profile reconciliation via setProfileReconcileCallback.
    void maskScriptSnapshot(toml::table& snapshot) const;

    // Invoked after native profile/panel reconciliation on startup, reload, plugin
    // changes, apply and clear, so the host republishes the masked script snapshot.
    // The callback must not re-enter reconciliation. Never called from frame paths.
    using ProfileReconcileCallback = std::function<void()>;
    void setProfileReconcileCallback(ProfileReconcileCallback callback);

    // Keeps the owned persistent passive panels (q/skin-runtime:music and
    // q/skin-runtime:desktop-status) open iff the applied recipe is a Stagecraft topology,
    // and synchronizes the process-only geometry profile with the currently active
    // valid receipt. The wallpaper palette stays authoritative. Called from existing
    // startup / config-reload / plugin-change hooks, after apply and clear — never
    // from a frame path.
    // Idempotent: an already-correct panel is never closed and reopened, so repeated
    // config reloads do not make them flicker.
    void reconcilePersistentPanels();

  private:
    // Applies the process-only visual profile for `recipe` (nullopt restores the latest
    // underlying palette and disables Stagecraft styling via the frozen ui core APIs).
    void applyPresentationProfile(const std::optional<SkinRecipe>& recipe);
    // Outcome of making the presentation plugin discoverable and enabled.
    struct PluginRegistration {
      bool ok = false;
      std::string error;
      bool addedSource = false;
      bool enabledPlugin = false;
    };

    // The plugin's own user-layer settings (config files merged with settings.toml),
    // i.e. exactly the layer the skin overrides. nullopt when that layer is unreadable.
    using SettingList = std::vector<std::pair<std::string, std::string>>;

    // Ensures q/skin-runtime is installed, discovered, and enabled, adding only what is
    // missing. A recipe that nothing would consume must never report success.
    [[nodiscard]] PluginRegistration ensurePresentationPlugin();
    // Reverts only what this skin registered; a root another plugin also uses is kept.
    void revertPluginRegistration(bool addedSource, bool enabledPlugin);
    // Whether an already-registered enabled source root provides the packaged plugin.
    [[nodiscard]] bool registeredSourceCovers(const std::string& root) const;
    // Whether any plugin other than q/skin-runtime is loaded from the given root.
    [[nodiscard]] bool otherPluginDependsOnSource(const std::string& root) const;

    [[nodiscard]] std::optional<SkinReceipt> readReceipt() const;
    [[nodiscard]] std::optional<SkinRecipe> activeRecipe() const;
    // Setting value the q/skin-runtime runtime would see: the configured value, else
    // the manifest default. nullopt when neither exists.
    [[nodiscard]] std::optional<std::string> effectiveSetting(std::string_view key) const;
    [[nodiscard]] std::optional<SettingList> userLayerSettings() const;

    ConfigService& m_config;
    PanelManager& m_panels;
    ProfileReconcileCallback m_profileReconcile;
  };

} // namespace noctalia::shell::skins
