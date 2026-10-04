#pragma once

// Native desktop "jelly cat" control: pointer manipulation plus music-reactive
// motion and milk tint. Derived from the KISA widget, MIT licensed:
// https://github.com/howdeploy/kisa-stack revision
// 25744fa0afe9a0028e45123451b15fac36c04596; see licenses/howdeploy-kisa-MIT.
//
// The control owns only motion/input/tint state and pushes it into its
// JellyCatNode style; all rendering lives in the render slice.

#include "render/scene/input_area.h"
#include "render/scene/jelly_cat_node.h"
#include "ui/palette.h"
#include "ui/signal.h"
#include "ui/visuals/jelly_cat_motion.h"

#include <array>
#include <cstddef>
#include <functional>
#include <span>

class Renderer;

class JellyCat : public JellyCatNode {
public:
  static constexpr std::size_t kBandCount = 24;

  JellyCat();
  ~JellyCat() override;

  // Set by the host on palette/theme changes too (see the palette connection).
  void setMilkColor(const ColorSpec& color);
  // Music reactivity, clamped to [0.1, 3]; 1.0 reproduces the original impulse.
  void setSensitivity(float sensitivity);
  // Normalized 24-band magnitudes. Invalid sizing or non-finite/out-of-range
  // bands are treated as unavailable audio and cannot inject motion energy.
  void setValues(std::span<const float> values);
  // Audio became unavailable/disabled: resets spectrum history and audio targets
  // without clipping the springs' physical momentum.
  void clearSpectrum();
  [[nodiscard]] bool needsFrameTick() const noexcept;
  void tick(float deltaMs);
  // Requests a host frame tick/redraw; only queues, never rebuilds the scene.
  void setWakeCallback(std::function<void()> callback);
  void resetMotion();

protected:
  void doLayout(Renderer& renderer) override;

private:
  static constexpr float kPitch = 0.38F;
  static constexpr float kInitialYaw = 0.68F;
  // Original NekoWidget dragged with sensitivity 12; that hard-coded factor is
  // preserved so the pointer feel is unchanged while sensitivity drives audio.
  static constexpr float kPointerExciteScale = 12.0F * 1.25F;
  static constexpr float kPointerTwistScale = 3.0F;

  void onPointerPress(const InputArea::PointerData& data);
  void onPointerMotion(const InputArea::PointerData& data);
  void onPointerCancel();
  void beginHold(const InputArea::PointerData& data);
  void endHold();
  void poke(float x, float y);
  void excite(float x, float z, float squash, float twist);
  void requestWake();
  void refreshMilk();
  void pushStyle();
  [[nodiscard]] Color milkTargetColor() const;
  void updateAudio(float dt);
  void updateTint(float dt);
  [[nodiscard]] bool audioAnimating() const noexcept;

  JellyCatMotion m_motion;
  InputArea* m_inputArea = nullptr;

  std::array<float, kBandCount> m_levels{};
  std::array<float, kBandCount> m_previousLevels{};
  bool m_historyArmed = false;
  bool m_audioPresent = false;
  float m_rawEnergy = 0.0F;
  float m_bass = 0.0F;
  float m_energy = 0.0F;
  float m_energyTarget = 0.0F;
  float m_bassFloor = 0.0F;
  float m_pulse = 0.0F;
  float m_flowPhase = 0.0F;
  float m_flowTarget = 0.0F;
  bool m_tintAnimating = false;

  ColorSpec m_milkColorSpec = fixedColorSpec(rgba(243.0F / 255.0F, 237.0F / 255.0F, 221.0F / 255.0F, 1.0F));
  Color m_milkBase{};
  Color m_displayMilk{};

  float m_yaw = kInitialYaw;
  bool m_squeezing = false;
  float m_squeeze = 0.0F;
  bool m_pressed = false;
  bool m_dragging = false;
  float m_previousX = 0.0F;
  float m_pressX = 0.0F;
  float m_pressY = 0.0F;
  float m_pressSquash = 0.0F;
  // Physical button state, tracked separately from the interaction so pressing
  // the opposite button mid-drag cannot reset or prematurely release it.
  bool m_holdingLeft = false;
  bool m_holdingRight = false;

  float m_sensitivity = 1.0F;
  std::function<void()> m_wakeCallback;
  Signal<>::ScopedConnection m_paletteConn;
};
