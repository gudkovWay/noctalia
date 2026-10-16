#pragma once

#include "shell/panel/panel.h"

#include <memory>
#include <string>
#include <vector>

class ConfigService;
class TrayService;
class TrayWidget;

class TrayDrawerPanel : public Panel {
public:
  TrayDrawerPanel(TrayService* tray, ConfigService* config);
  ~TrayDrawerPanel() override;

  void create() override;
  void onClose() override;
  void onOpen(std::string_view context) override;
  [[nodiscard]] bool isContextActive(std::string_view context) const override;
  [[nodiscard]] bool fanPresentation() const;
  [[nodiscard]] bool hasDecoration() const override { return !fanPresentation(); }
  void setFanReveal(float progress);

  [[nodiscard]] float preferredWidth() const override;
  [[nodiscard]] float preferredHeight() const override;
  [[nodiscard]] PanelPlacement panelPlacement() const noexcept override;
  [[nodiscard]] LayerShellKeyboard keyboardMode() const override { return LayerShellKeyboard::OnDemand; }
  void setAnimationManager(AnimationManager* mgr) noexcept override;

private:
  void doLayout(Renderer& renderer, float width, float height) override;
  void doUpdate(Renderer& renderer) override;
  [[nodiscard]] std::size_t currentDrawerColumns() const;
  [[nodiscard]] std::optional<float> currentDrawerItemSize() const;
  [[nodiscard]] float resolvedItemGap() const;
  [[nodiscard]] std::size_t visibleItemCount() const;
  [[nodiscard]] std::string_view sourceWidgetName() const;

  TrayService* m_tray = nullptr;
  ConfigService* m_config = nullptr;
  std::unique_ptr<TrayWidget> m_drawerWidget;
  std::string m_sourceWidgetName;
  float m_fanProgress = 0.0F;
  mutable std::optional<bool> m_fanPresentation;
};
