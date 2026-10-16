#pragma once

#include "render/core/render_styles.h"

#include <array>
#include <string_view>

namespace desktop_audio {
  struct Preset {
    std::string_view type;
    FancyAudioVisualizerMode mode;
    float width;
    float height;
    float sensitivity;
    float thickness;
    float bloom;
  };

  inline constexpr std::array kPresets{
      Preset{"audio_ribbons", FancyAudioVisualizerMode::Ribbons, 480.0F, 160.0F, 1.3F, 1.2F, 0.25F},
      Preset{"audio_contour", FancyAudioVisualizerMode::Contour, 480.0F, 150.0F, 1.4F, 0.8F, 0.2F},
      Preset{"audio_radial_bars", FancyAudioVisualizerMode::Bars, 300.0F, 300.0F, 1.4F, 1.0F, 0.25F},
      Preset{"audio_wave_ring", FancyAudioVisualizerMode::Wave, 300.0F, 300.0F, 1.3F, 0.7F, 0.3F},
      Preset{"audio_mist", FancyAudioVisualizerMode::Mist, 540.0F, 180.0F, 1.2F, 0.7F, 0.0F},
      Preset{"audio_ornament", FancyAudioVisualizerMode::Ornament, 300.0F, 340.0F, 1.2F, 0.8F, 0.25F},
  };

  [[nodiscard]] constexpr const Preset* preset(std::string_view type) noexcept {
    for (const auto& entry : kPresets)
      if (entry.type == type)
        return &entry;
    return nullptr;
  }

  [[nodiscard]] constexpr bool isVisualizer(std::string_view type) noexcept {
    return type == "audio_visualizer" || type == "fancy_audio_visualizer" || preset(type) != nullptr;
  }
} // namespace desktop_audio
