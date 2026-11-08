#pragma once

#include "shell/bar/widget.h"
#include "shell/bar/widget_custom_image.h"

#include <cstdint>
#include <string>
#include <unordered_map>

class EasyEffectsService;
class Glyph;
class Image;
class Label;
class PipeWireService;
struct wl_output;
class Slider;
class Box;

enum class VolumeWidgetTarget {
  Output,
  Input,
};

class VolumeWidget : public Widget {
public:
  struct Options {
    VolumeWidgetTarget device = VolumeWidgetTarget::Output;
    std::string glyph;
    std::string muteGlyph;
    std::unordered_map<std::string, std::string> effectsProfileGlyphs;
    std::string customImage;
    bool customImageColorize = false;
    bool showLabel = true;
    bool slider = false;
    bool sliderShowThumb = true;
    std::string sliderFillImage;
    float maximumVolume = 100.0F;
    bool hideWhenInactive = false;
    ColorSpec muteColor = colorSpecFromRole(ColorRole::Error);
  };

  VolumeWidget(PipeWireService* audio, EasyEffectsService* easyEffects, Options options);

  void create() override;

private:
  void doLayout(Renderer& renderer, float containerWidth, float containerHeight) override;
  void doUpdate(Renderer& renderer) override;
  void syncState(Renderer& renderer);
  void syncWidgetVisibility(bool showWidget);
  void syncSliderState();
  void syncSliderProgress();
  [[nodiscard]] std::string glyphName(float volume, bool muted, const std::string& effectsProfile = {}) const;

  PipeWireService* m_audio = nullptr;
  EasyEffectsService* m_easyEffects = nullptr;
  bool m_showLabel = true;
  bool m_sliderMode = false;
  bool m_sliderShowThumb = true;
  float m_maximumVolume = 100.0F;
  VolumeWidgetTarget m_target = VolumeWidgetTarget::Output;
  ColorSpec m_muteColor;
  std::string m_glyphOverride;
  std::string m_muteGlyphOverride;
  std::unordered_map<std::string, std::string> m_effectsProfileGlyphs;
  WidgetCustomImage m_customImage;
  WidgetCustomImage m_sliderFillSource;
  // Only ever true for the input widget: "inactive" means no application is capturing audio.
  bool m_hideWhenInactive = false;
  Glyph* m_glyph = nullptr;
  Image* m_image = nullptr;
  Label* m_label = nullptr;
  float m_lastVolume = -1.0F;
  std::string m_lastEffectsProfile;
  bool m_lastMuted = false;
  Slider* m_slider = nullptr;
  Box* m_sliderThumb = nullptr;
  Node* m_sliderFillClip = nullptr;
  Image* m_sliderFillImage = nullptr;
  bool m_isVertical = false;
  bool m_lastVertical = false;
  bool m_lastMicActive = false;
};
