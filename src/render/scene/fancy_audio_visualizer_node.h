#pragma once

#include "render/core/render_styles.h"
#include "render/core/texture_handle.h"
#include "render/scene/node.h"

#include <array>
#include <cstdint>
#include <span>

class TextureManager;

class FancyAudioVisualizerNode : public Node {
public:
  static constexpr int kBandCount = 32;
  static constexpr int kTextureWidth = 128;

  FancyAudioVisualizerNode() : Node(NodeType::FancyAudioVisualizer) {}
  ~FancyAudioVisualizerNode() override;

  FancyAudioVisualizerNode(const FancyAudioVisualizerNode&) = delete;
  FancyAudioVisualizerNode& operator=(const FancyAudioVisualizerNode&) = delete;

  [[nodiscard]] const FancyAudioVisualizerStyle& style() const noexcept { return m_style; }
  [[nodiscard]] TextureId textureId() const noexcept { return m_texture.id; }
  [[nodiscard]] int textureWidth() const noexcept { return m_texWidth; }

  void setStyle(const FancyAudioVisualizerStyle& style) {
    if (m_style == style) {
      return;
    }
    m_style = style;
    markPaintDirty();
  }

  bool setSpectrumValues(TextureManager& textures, std::span<const float> values);
  bool setAudioData(TextureManager& textures, std::span<const float> spectrum, std::span<const float> history);

private:
  void doInvalidateGpuResources(Renderer& renderer) override;

  FancyAudioVisualizerStyle m_style;
  TextureManager* m_textureManager = nullptr;
  TextureHandle m_texture;
  std::array<std::uint8_t, kTextureWidth * 4> m_pixels{};
  bool m_hasPixels = false;
  int m_texWidth = 0;
};
