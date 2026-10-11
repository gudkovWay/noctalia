#pragma once

// A modal can end on key-down. Keep its Cancel press owned until release so
// repeats cannot fall through to host dismissal after the script leaves the mode.
class PanelCancelCapture {
public:
  enum class Decision { Pass, Deliver, Repeat };

  void setEnabled(bool enabled) noexcept { m_enabled = enabled; }

  [[nodiscard]] Decision handle(bool pressed) noexcept {
    if (!m_enabled && !m_held) {
      return Decision::Pass;
    }
    if (pressed && m_held) {
      return Decision::Repeat;
    }
    m_held = pressed;
    return Decision::Deliver;
  }

  void reset() noexcept {
    m_enabled = false;
    m_held = false;
  }

private:
  bool m_enabled = false;
  bool m_held = false;
};
