#include "shell/tray/tray_drawer_panel.h"

#include "config/config_service.h"
#include "dbus/tray/tray_service.h"
#include "shell/bar/widgets/tray_widget.h"
#include "shell/panel/panel_manager.h"
#include "shell/tray/tray_fan_geometry.h"
#include "shell/tray/tray_fold_geometry.h"
#include "shell/tray/tray_identifier.h"
#include "shell/tray/tray_settings.h"
#include "util/string_utils.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "util/file_utils.h"

#include <algorithm>
#include <vector>

TrayDrawerPanel::TrayDrawerPanel(TrayService* tray, ConfigService* config) : m_tray(tray), m_config(config) {}

TrayDrawerPanel::~TrayDrawerPanel() = default;
std::string_view TrayDrawerPanel::sourceWidgetName() const {
  const std::string_view name = !pendingOpenContext().empty() ? pendingOpenContext()
      : !m_sourceWidgetName.empty()                           ? std::string_view(m_sourceWidgetName)
                                                              : tray::kCanonicalTrayWidgetName;
  if (m_config != nullptr && name != tray::kCanonicalTrayWidgetName) {
    const auto found = m_config->config().widgets.find(std::string(name));
    if (found == m_config->config().widgets.end() || found->second.type != "tray")
      return tray::kCanonicalTrayWidgetName;
  }
  return name;
}

const BarConfig* TrayDrawerPanel::sourceBar() const {
  if (m_config == nullptr)
    return nullptr;
  const auto name = sourceWidgetName();
  for (const auto& bar : m_config->config().bars) {
    if (std::ranges::find(bar.startWidgets, name) != bar.startWidgets.end()
        || std::ranges::find(bar.centerWidgets, name) != bar.centerWidgets.end()
        || std::ranges::find(bar.endWidgets, name) != bar.endWidgets.end())
      return &bar;
  }
  return m_config->config().bars.empty() ? nullptr : &m_config->config().bars.front();
}

std::string_view TrayDrawerPanel::materialPath() const noexcept {
  if (m_config == nullptr)
    return {};
  const auto source = sourceWidgetName();
  for (const auto& [name, widget] : m_config->config().widgets) {
    if (name != source)
      continue;
    for (const auto& [key, value] : widget.settings) {
      if (key == "drawer_background_image") {
        if (const auto* path = std::get_if<std::string>(&value))
          return *path;
        return {};
      }
    }
    return {};
  }
  return {};
}

bool TrayDrawerPanel::listPresentation() const {
  return m_config != nullptr
      && tray::resolvedTrayOptions(*m_config, {}, sourceWidgetName()).options.drawerList;
}

bool TrayDrawerPanel::fanPresentation() const {
  if (m_fanPresentation)
    return *m_fanPresentation;
  m_fanPresentation =
      m_config != nullptr
      && sourceWidgetName() == "q_skin_runtime_tray"
      && std::ranges::any_of(m_config->config().bars, [](const BarConfig& bar) {
           return bar.name == "skin-runtime-status"
               && bar.position == "top"
               && std::ranges::find(bar.endWidgets, std::string_view("q_skin_runtime_tray")) != bar.endWidgets.end();
         });
  return *m_fanPresentation;
}

void TrayDrawerPanel::onOpen(std::string_view context) {
  m_sourceWidgetName = context.empty() ? std::string(tray::kCanonicalTrayWidgetName) : std::string(context);
  setPendingOpenContext({});
}

bool TrayDrawerPanel::isContextActive(std::string_view context) const { return context == sourceWidgetName(); }

void TrayDrawerPanel::setDrawerReveal(float progress) {
  m_revealProgress = progress;
  if (m_drawerWidget)
    m_drawerWidget->setDrawerReveal(progress);
}

PanelPlacement TrayDrawerPanel::panelPlacement() const noexcept {
  if (fanPresentation())
    return PanelPlacement::Floating;
  if (m_config == nullptr) {
    return PanelPlacement::Attached;
  }
  return tray::resolvedTrayOptions(*m_config, {}, sourceWidgetName()).options.detachedPanel ? PanelPlacement::Floating
                                                                                            : PanelPlacement::Attached;
}

std::optional<float> TrayDrawerPanel::currentDrawerItemSize() const {
  return m_config != nullptr ? tray::resolvedTrayOptions(*m_config, {}, sourceWidgetName()).drawerItemSize
                             : std::nullopt;
}

float TrayDrawerPanel::resolvedItemGap() const {
  float gap = Style::spaceXs;
  if (m_config == nullptr) {
    return gap;
  }

  const auto resolved = tray::resolvedTrayOptions(*m_config, {}, sourceWidgetName());
  const auto* bar = sourceBar();
  if (bar != nullptr)
    gap = static_cast<float>(bar->widgetSpacing);
  if (!resolved.options.matchAdjacentSpacing || bar == nullptr) {
    return gap;
  }
  const auto capsule = tray::resolvedTrayCapsuleSpec(*m_config, *bar, sourceWidgetName());
  const float padding = capsule.enabled ? capsule.padding * contentScale() : 0.0F;
  return gap + 2.0F * padding;
}

float TrayDrawerPanel::preferredWidth() const {
  if (fanPresentation())
    return scaled(tray::kFanExtent);
  if (!materialPath().empty())
    return tray::FoldLayout(visibleItemCount(), 0, contentScale(), listPresentation()).width();
  const float itemSize = scaled(currentDrawerItemSize().value_or(Style::baseGlyphSize));
  const float gap = resolvedItemGap();
  const std::size_t drawerColumns = currentDrawerColumns();
  const std::size_t cols = std::min<std::size_t>(drawerColumns, std::max<std::size_t>(1, visibleItemCount()));
  const float contentWidth = static_cast<float>(cols) * itemSize + static_cast<float>(cols > 1 ? cols - 1 : 0) * gap;
  const float panelPadding = scaled(Style::panelPadding) * 2.0F;
  return contentWidth + panelPadding;
}

float TrayDrawerPanel::preferredHeight() const {
  if (fanPresentation())
    return scaled(tray::kFanExtent);
  if (!materialPath().empty())
    return tray::FoldLayout(visibleItemCount(), 0, contentScale(), listPresentation()).height();
  const float itemSize = scaled(currentDrawerItemSize().value_or(Style::baseGlyphSize));
  const float gap = resolvedItemGap();
  const std::size_t count = std::max<std::size_t>(1, visibleItemCount());
  const std::size_t drawerColumns = currentDrawerColumns();
  const std::size_t rows = (count + drawerColumns - 1U) / drawerColumns;
  const float contentHeight = static_cast<float>(rows) * itemSize + static_cast<float>(rows > 1 ? rows - 1 : 0) * gap;
  const float panelPadding = scaled(Style::panelPadding) * 2.0F;
  return contentHeight + panelPadding;
}

void TrayDrawerPanel::create() {
  if (m_config == nullptr) {
    return;
  }

  float widgetSpacing = Style::spaceXs;
  std::string barPosition = "top";
  if (const auto* barConfig = sourceBar()) {
    widgetSpacing = static_cast<float>(barConfig->widgetSpacing);
    barPosition = barConfig->position;
  }

  auto resolved = tray::resolvedTrayOptions(
      *m_config,
      TrayWidgetDefinitionContext{
          .barPosition = barPosition,
          .inlineEntryGap = widgetSpacing,
      },
      sourceWidgetName()
  );
  auto options = std::move(resolved.options);
  options.drawerMode = false;
  options.itemActivated = []() { PanelManager::instance().close(); };
  options.panelGridMode = true;
  options.customItemSize = resolved.drawerItemSize;
  options.fanPanel = fanPresentation();
  m_drawerWidget = std::make_unique<TrayWidget>(*m_config, m_tray, std::move(options));
  m_drawerWidget->setWidgetIconColor(resolved.iconColor);
  m_drawerWidget->setContentScale(contentScale());
  if (const auto* barConfig = sourceBar()) {
    m_drawerWidget->setBarCapsuleSpec(tray::resolvedTrayCapsuleSpec(*m_config, *barConfig, sourceWidgetName()));
  }
  m_drawerWidget->setAnimationManager(m_animations);
  m_drawerWidget->setUpdateCallback([]() { PanelManager::instance().requestUpdateOnly(); });
  m_drawerWidget->setRedrawCallback([]() { PanelManager::instance().requestRedraw(); });
  m_drawerWidget->setFrameTickRequestCallback([]() { PanelManager::instance().requestFrameTick(); });
  m_drawerWidget->setPanelToggleCallback([](std::string_view id, std::string_view context, std::optional<float> ax,
                                            std::optional<float> ay, Widget::PanelActivation activation) {
    PanelOpenRequest request{
        .anchorX = ax.has_value() ? std::round(*ax) : 0.0F,
        .anchorY = ay.has_value() ? std::round(*ay) : 0.0F,
        .hasAnchorPosition = ax.has_value() && ay.has_value(),
        .context = std::string(context),
    };
    if (activation == Widget::PanelActivation::Open) {
      PanelManager::instance().openPanel(std::string(id), request);
    } else {
      PanelManager::instance().togglePanel(std::string(id), request);
    }
  });
  m_drawerWidget->create();
  if (materialPath().empty()) {
    setRoot(m_drawerWidget->releaseRoot());
  } else {
    auto root = ui::box({.fill = fixedColorSpec(rgba(0, 0, 0, 0))});
    root->addChild(ui::image({.out = &m_material, .fit = ImageFit::Stretch}));
    root->addChild(m_drawerWidget->releaseRoot());
    setRoot(std::move(root));
  }
  m_drawerWidget->setDrawerReveal(m_revealProgress);
}

void TrayDrawerPanel::onClose() {
  m_drawerWidget.reset();
  clearReleasedRoot();
  m_sourceWidgetName.clear();
  setPendingOpenContext({});
  m_revealProgress = 0;
  m_material = nullptr;
  m_loadedMaterial.clear();
  m_fanPresentation.reset();
}

void TrayDrawerPanel::setAnimationManager(AnimationManager* mgr) noexcept {
  Panel::setAnimationManager(mgr);
  if (m_drawerWidget != nullptr) {
    m_drawerWidget->setAnimationManager(mgr);
  }
}

void TrayDrawerPanel::doLayout(Renderer& renderer, float width, float height) {
  if (m_drawerWidget == nullptr || m_drawerWidget->root() == nullptr) {
    return;
  }
  const bool material = m_material != nullptr;
  m_drawerWidget->layout(renderer, width, height);
  if (material) {
    root()->setSize(width, height);
    m_material->setSize(width, height);
    const auto path = materialPath();
    if (path != m_loadedMaterial) {
      m_loadedMaterial = path;
      m_material->setSourceFile(renderer, FileUtils::expandUserPath(m_loadedMaterial).string(),
                               static_cast<int>(std::max(width, height) * 2), true);
    }
    auto* content = m_drawerWidget->root();
    content->setPosition(0, 0);
  }
}

void TrayDrawerPanel::doUpdate(Renderer& renderer) {
  if (m_drawerWidget == nullptr || m_drawerWidget->root() == nullptr) {
    return;
  }
  m_drawerWidget->update(renderer);
}

std::size_t TrayDrawerPanel::currentDrawerColumns() const {
  return m_config != nullptr ? tray::resolvedTrayOptions(*m_config, {}, sourceWidgetName()).options.panelGridColumns
                             : TrayWidget::Options{}.panelGridColumns;
}

std::size_t TrayDrawerPanel::visibleItemCount() const {
  if (m_tray == nullptr) {
    return 0;
  }
  const auto options = m_config != nullptr ? tray::resolvedTrayOptions(*m_config, {}, sourceWidgetName()).options
                                           : TrayWidget::Options{};
  const auto& hiddenItems = options.hiddenItems;
  const auto& pinnedItems = options.pinnedItems;
  std::vector<std::string> hiddenLower;
  hiddenLower.reserve(hiddenItems.size());
  for (const auto& v : hiddenItems) {
    hiddenLower.push_back(StringUtils::toLower(v));
  }
  std::vector<std::string> pinnedLower;
  pinnedLower.reserve(pinnedItems.size());
  for (const auto& v : pinnedItems) {
    pinnedLower.push_back(StringUtils::toLower(v));
  }
  auto hasVariant = [&](std::string_view token, std::string_view value) {
    std::string raw(value);
    if (raw.empty()) {
      return false;
    }
    std::vector<std::string> variants;
    variants.push_back(raw);
    variants.push_back(StringUtils::toLower(raw));
    if (const auto slash = raw.find_last_of('/'); slash != std::string::npos && slash + 1 < raw.size()) {
      variants.push_back(raw.substr(slash + 1));
      variants.push_back(StringUtils::toLower(raw.substr(slash + 1)));
    }
    return std::ranges::contains(variants, token);
  };
  auto tokenMatches = [&](std::string_view token, const TrayItemInfo& item) {
    if (token.empty()) {
      return false;
    }
    const auto lowered = StringUtils::toLower(token);
    return hasVariant(lowered, item.id)
        || hasVariant(lowered, item.busName)
        || hasVariant(lowered, item.itemName)
        || hasVariant(lowered, item.processName)
        || hasVariant(lowered, item.objectPath)
        || hasVariant(lowered, item.iconName)
        || hasVariant(lowered, item.overlayIconName)
        || hasVariant(lowered, item.attentionIconName);
  };
  std::size_t visible = 0;
  for (const auto& item : m_tray->items()) {
    if (options.hidePassive && tray::isPassiveStatus(item)) {
      continue;
    }
    const bool hidden =
        std::ranges::any_of(hiddenLower, [&](const std::string& token) { return tokenMatches(token, item); });
    const bool pinned =
        std::ranges::any_of(pinnedLower, [&](const std::string& token) { return tray::tokenMatchesItem(token, item); });
    if (!hidden && (fanPresentation() || !pinned)) {
      ++visible;
    }
  }
  return visible;
}
