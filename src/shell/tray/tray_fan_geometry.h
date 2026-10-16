#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

namespace tray {

  inline constexpr std::size_t kFanPageSize = 8;
  inline constexpr float kFanExtent = 416.0F;
  inline constexpr float kFanInset = 12.0F;
  inline constexpr float kFanIconSize = 28.0F;
  inline constexpr float kFanSweep = std::numbers::pi_v<float> * 0.5F;

  struct FanPoint {
    float x;
    float y;
  };

  class FanLayout {
  public:
    FanLayout(std::size_t items, std::size_t requestedPage, float extent)
        : m_items(items), m_page(std::min(requestedPage, pages() - 1)), m_scale(std::max(1.0F, extent) / kFanExtent) {}
    [[nodiscard]] std::size_t pages() const {
      return std::max<std::size_t>(1, (m_items + kFanPageSize - 1) / kFanPageSize);
    }
    [[nodiscard]] std::size_t page() const { return m_page; }
    [[nodiscard]] std::size_t first() const { return m_page * kFanPageSize; }
    [[nodiscard]] std::size_t count() const { return std::min(kFanPageSize, m_items - first()); }
    [[nodiscard]] float scale() const { return m_scale; }
    [[nodiscard]] float radius() const { return (kFanExtent - 2.0F * kFanInset) * m_scale; }
    [[nodiscard]] FanPoint pivot() const { return {(kFanExtent - kFanInset) * m_scale, kFanInset * m_scale}; }
    [[nodiscard]] FanPoint appPosition(std::size_t slot) const {
      const float angle =
          (static_cast<float>(slot) + 0.5F) * kFanSweep / static_cast<float>(std::max<std::size_t>(1, count()));
      const auto origin = pivot();
      return {origin.x - radius() * 0.9F * std::cos(angle), origin.y + radius() * 0.9F * std::sin(angle)};
    }
    [[nodiscard]] std::optional<std::size_t> sectorAt(float x, float y) const {
      if (count() == 0)
        return std::nullopt;
      const auto origin = pivot();
      const float dx = origin.x - x, dy = y - origin.y;
      const float squared = dx * dx + dy * dy;
      if (dx < 0 || dy < 0 || squared < radius() * radius() * 0.04F || squared > radius() * radius())
        return std::nullopt;
      const float angle = std::atan2(dy, dx);
      return std::min(count() - 1, static_cast<std::size_t>(angle * static_cast<float>(count()) / kFanSweep));
    }

  private:
    std::size_t m_items;
    std::size_t m_page;
    float m_scale;
  };

  [[nodiscard]] inline float fanLeafAngle(std::size_t leaf, float progress, std::size_t leaves) {
    const float p = std::clamp(progress, 0.0F, 1.0F);
    return -(
        kFanSweep * (1.0F - p)
        + static_cast<float>(leaf) * kFanSweep / static_cast<float>(std::max<std::size_t>(1, leaves)) * p
    );
  }
  [[nodiscard]] inline float fanContentOpacity(float progress) {
    return std::clamp((progress - 0.75F) * 4.0F, 0.0F, 1.0F);
  }
  [[nodiscard]] inline bool fanActionsEnabled(float progress) { return progress >= 1.0F; }
  [[nodiscard]] inline float fanRevealDuration(float progress, bool opening) {
    const float p = std::clamp(progress, 0.0F, 1.0F);
    return (opening ? 340.0F : 220.0F) * (opening ? 1.0F - p : p);
  }

} // namespace tray
