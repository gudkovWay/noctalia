#include "render/scene/fancy_audio_visualizer_node.h"

#include "render/core/texture_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

FancyAudioVisualizerNode::~FancyAudioVisualizerNode() {
  if (m_textureManager != nullptr) {
    m_textureManager->unload(m_texture);
  }
}

bool FancyAudioVisualizerNode::setSpectrumValues(TextureManager& textures, std::span<const float> values) {
  return setAudioData(textures, values, {});
}

bool FancyAudioVisualizerNode::setAudioData(
    TextureManager& textures, std::span<const float> spectrum, std::span<const float> history
) {
  if (m_textureManager != nullptr && m_textureManager != &textures) {
    m_textureManager->unload(m_texture);
    m_texture = {};
  }
  m_textureManager = &textures;
  std::array<std::uint8_t, kTextureWidth * 4> pixels{};
  const auto quantize = [](float value) {
    return static_cast<std::uint8_t>((std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F) * 255.0F);
  };
  for (std::size_t i = 0; i < kTextureWidth; ++i) {
    pixels[i * 4] = quantize(i < kBandCount && i < spectrum.size() ? spectrum[i] : 0.0F);
    pixels[i * 4 + 1] = quantize(i < history.size() ? history[i] : 0.0F);
    pixels[i * 4 + 3] = 255;
  }
  if (m_texture.valid() && m_hasPixels && m_pixels == pixels)
    return false;
  m_pixels = pixels;
  m_hasPixels = true;
  if (m_texture.id == 0) {
    if (!textures.replace(
            m_texture, m_pixels.data(), kTextureWidth, 1, TextureDataFormat::Rgba, TextureFilter::Nearest
        )) {
      return false;
    }
  } else if (!textures.updateSubImage(m_texture, m_pixels.data(), 0, 0, kTextureWidth, 1, TextureDataFormat::Rgba)) {
    // Keep a failed upload retryable even when the next audio frame is identical.
    textures.unload(m_texture);
    m_texture = {};
    return false;
  }
  m_texWidth = kTextureWidth;
  markPaintDirty();
  return true;
}

void FancyAudioVisualizerNode::doInvalidateGpuResources(Renderer& renderer) {
  (void)renderer;
  if (m_textureManager == nullptr) {
    m_texture = {};
    m_texWidth = 0;
    return;
  }

  if (m_texture.id != 0) {
    m_textureManager->unload(m_texture);
  }
  m_texture = {};
  m_texWidth = 0;

  if (!m_hasPixels) {
    markPaintDirty();
    return;
  }

  if (m_textureManager->replace(
          m_texture, m_pixels.data(), kTextureWidth, 1, TextureDataFormat::Rgba, TextureFilter::Nearest
      )) {
    m_texWidth = kTextureWidth;
  }
  markPaintDirty();
}
