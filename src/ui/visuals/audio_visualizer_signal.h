#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

// One state per desktop instance; all instances consume the shared PipeWire stream.
// History is chronological RMS energy, not a waveform or a rearranged FFT.
class AudioVisualizerSignal {
public:
  static constexpr std::size_t kBands = 32;
  static constexpr std::size_t kHistory = 128;

  void accept(std::span<const float> values) noexcept {
    for (std::size_t i = 0; i < kBands; ++i) {
      const float value = i < values.size() ? values[i] : 0.0F;
      m_target[i] = std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
    }
  }

  void step(float deltaMs, bool active) noexcept {
    if (!active) {
      m_target.fill(0.0F);
      m_spectrum.fill(0.0F);
      m_history.fill(0.0F);
      m_elapsed = 0.0F;
      return;
    }
    if (!std::isfinite(deltaMs) || deltaMs <= 0.0F)
      return;
    // A stalled shell must not fabricate a long history from one stale sample.
    if (deltaMs > 250.0F) {
      m_history.fill(0.0F);
      m_elapsed = 0.0F;
      deltaMs = 50.0F;
    }
    while (deltaMs > 0.0F) {
      const float dt = std::min(deltaMs, 50.0F - m_elapsed);
      float sum = 0.0F;
      for (std::size_t i = 0; i < kBands; ++i) {
        const float tau = m_target[i] > m_spectrum[i] ? 45.0F : 180.0F;
        m_spectrum[i] += (m_target[i] - m_spectrum[i]) * (-std::expm1(-dt / tau));
        if (m_spectrum[i] < 0.0001F)
          m_spectrum[i] = 0.0F;
        sum += m_spectrum[i] * m_spectrum[i];
      }
      m_elapsed += dt;
      deltaMs -= dt;
      if (m_elapsed >= 50.0F) {
        std::move(m_history.begin() + 1, m_history.end(), m_history.begin());
        m_history.back() = std::sqrt(sum / static_cast<float>(kBands));
        m_elapsed = 0.0F;
      }
    }
  }

  [[nodiscard]] const auto& spectrum() const noexcept { return m_spectrum; }
  [[nodiscard]] const auto& history() const noexcept { return m_history; }

private:
  std::array<float, kBands> m_target{};
  std::array<float, kBands> m_spectrum{};
  std::array<float, kHistory> m_history{};
  float m_elapsed = 0.0F;
};
