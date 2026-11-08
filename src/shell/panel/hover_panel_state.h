#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

class HoverPanelState {
public:
  using Key = std::uintptr_t;
  enum class Kind : std::uint8_t { None, Open, Close };
  struct Action {
    Kind kind = Kind::None;
    Key key = 0;
  };

  void trigger(Key key, bool inside, std::int64_t now) {
    if (inside) {
      m_hovered = key;
      m_closeAt.reset();
      if (m_active == key) {
        m_pending = 0;
        m_openAt.reset();
      } else if (m_pending != key) {
        m_pending = key;
        m_openAt = now + 120;
      }
    } else {
      if (m_hovered == key)
        m_hovered = 0;
      if (m_pending == key) {
        m_pending = 0;
        m_openAt.reset();
      }
      updateClose(now);
    }
  }

  void panel(bool inside, std::int64_t now) {
    m_panel = inside;
    updateClose(now);
  }

  void popup(bool open, std::int64_t now) {
    m_popup = open;
    updateClose(now);
  }

  [[nodiscard]] Action advance(std::int64_t now) {
    if (m_openAt && now >= *m_openAt && m_pending != 0 && m_pending == m_hovered) {
      m_active = m_pending;
      m_pending = 0;
      m_openAt.reset();
      m_closeAt.reset();
      m_panel = false;
      return {Kind::Open, m_active};
    }
    if (m_closeAt && now >= *m_closeAt) {
      const auto key = m_active;
      reset();
      return {Kind::Close, key};
    }
    return {};
  }

  [[nodiscard]] std::optional<std::int64_t> delay(std::int64_t now) const {
    std::optional<std::int64_t> deadline = m_openAt;
    if (m_closeAt)
      deadline = deadline ? std::min(*deadline, *m_closeAt) : m_closeAt;
    return deadline ? std::optional<std::int64_t>(std::max<std::int64_t>(1, *deadline - now)) : std::nullopt;
  }

  [[nodiscard]] Key activeKey() const noexcept { return m_active; }
  void reset() noexcept {
    m_hovered = m_pending = m_active = 0;
    m_openAt.reset();
    m_closeAt.reset();
    m_panel = m_popup = false;
  }

private:
  void updateClose(std::int64_t now) {
    if (m_active == 0 || m_hovered != 0 || m_pending != 0 || m_panel || m_popup) {
      m_closeAt.reset();
    } else if (!m_closeAt) {
      m_closeAt = now + 220;
    }
  }

  Key m_hovered = 0;
  Key m_pending = 0;
  Key m_active = 0;
  std::optional<std::int64_t> m_openAt;
  std::optional<std::int64_t> m_closeAt;
  bool m_panel = false;
  bool m_popup = false;
};
