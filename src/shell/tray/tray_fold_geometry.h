#pragma once

#include "shell/tray/tray_fan_geometry.h"

namespace tray {

  inline constexpr float kFoldWidth = 274.0F;
  inline constexpr float kFoldTargetSize = 52.0F;
  inline constexpr float kFoldIconSize = 32.0F;
  inline constexpr float kFoldAttachOverlap = 30.0F;

  class FoldLayout {
  public:
    FoldLayout(std::size_t items, std::size_t requestedPage, float scale = 1.0F, bool list = false)
        : m_items(items), m_scale(std::max(0.1F, scale)), m_list(list) {
      m_page = std::min(requestedPage, pages() - 1);
    }

    [[nodiscard]] std::size_t pages() const { return std::max<std::size_t>(1, (m_items + pageSize() - 1) / pageSize()); }
    [[nodiscard]] std::size_t page() const { return m_page; }
    [[nodiscard]] std::size_t first() const { return m_page * pageSize(); }
    [[nodiscard]] std::size_t count() const { return std::min(pageSize(), m_items - first()); }
    [[nodiscard]] float width() const { return (m_list ? 415.0F : kFoldWidth) * m_scale; }
    [[nodiscard]] float height() const {
      if (m_list)
        return 671.0F * m_scale;
      const auto rows = std::max<std::size_t>(1, (std::min(m_items, kFanPageSize) + 1) / 2);
      return (104.0F + 56.0F * static_cast<float>(rows)) * m_scale;
    }
    [[nodiscard]] FanPoint appPosition(std::size_t slot) const {
      if (m_list)
        return {65.0F * m_scale, (158.0F + 52.5F * static_cast<float>(slot)) * m_scale};
      const bool right = slot % 2 != 0;
      return {(right ? 174.0F : 86.0F) * m_scale,
              (76.0F + 56.0F * static_cast<float>(slot / 2) + (right ? 16.0F : 0.0F)) * m_scale};
    }
    [[nodiscard]] std::optional<std::size_t> itemAt(float x, float y) const {
      const float half = kFoldTargetSize * m_scale * 0.5F;
      for (std::size_t slot = 0; slot < count(); ++slot) {
        const auto point = appPosition(slot);
        if (m_list && x >= 39.0F * m_scale && x < 283.0F * m_scale
            && y >= point.y - half && y < point.y + half)
          return slot;
        if (x >= point.x - half && x < point.x + half && y >= point.y - half && y < point.y + half)
          return slot;
      }
      return std::nullopt;
    }

  private:
    std::size_t m_items;
    [[nodiscard]] std::size_t pageSize() const { return m_list ? 7U : kFanPageSize; }
    std::size_t m_page = 0;
    float m_scale;
    bool m_list = false;
  };

} // namespace tray
