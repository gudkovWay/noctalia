#pragma once

#include "config/config_types.h"
#include "shell/desktop/desktop_widget.h"
#include "ui/palette.h"

#include <cstdint>

class JellyCat;
class PipeWireSpectrum;

// Procedural jelly cat desktop widget. Reacts to pointer input always and to the
// shared PipeWireSpectrum only when music is enabled; never owns or retunes the
// capture. Spectrum values are pulled in the update/frame phase, never from the
// listener callback.
class DesktopJellyCatWidget : public DesktopWidget {
public:
  struct Options {
    float size = 210.0F;
    ColorSpec color = colorSpecFromConfigString("#f3eddd", "color");
    float sensitivity = 1.0F;
    bool musicEnabled = true;
  };

  DesktopJellyCatWidget(PipeWireSpectrum* spectrum, Options options);
  ~DesktopJellyCatWidget() override;

  void create() override;
  bool applySetting(
      const std::string& key, const WidgetSettingValue& value,
      const std::unordered_map<std::string, WidgetSettingValue>& allSettings, Renderer& renderer
  ) override;
  [[nodiscard]] bool needsFrameTick() const override;
  void onFrameTick(float deltaMs, Renderer& renderer) override;

private:
  void subscribeSpectrum();
  void unsubscribeSpectrum(bool clearValues);
  void pullSpectrumValues();
  void doLayout(Renderer& renderer) override;
  void doUpdate(Renderer& renderer) override;

  static constexpr int kSpectrumBands = 24;

  PipeWireSpectrum* m_spectrum = nullptr;
  float m_size = 210.0F;
  ColorSpec m_color = colorSpecFromConfigString("#f3eddd", "color");
  float m_sensitivity = 1.0F;
  bool m_musicEnabled = true;
  JellyCat* m_cat = nullptr;
  std::uint64_t m_listenerId = 0;
  bool m_pendingSpectrum = false;
};
