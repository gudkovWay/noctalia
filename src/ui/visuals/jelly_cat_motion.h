#pragma once

// Eight-spring gel motion ported from KISA's NekoMotion.js.
//
// Derived from howdeploy/kisa-stack revision 25744fa0afe9a0028e45123451b15fac36c04596,
// MIT licensed: https://github.com/howdeploy/kisa-stack; see
// licenses/howdeploy-kisa-MIT for the full upstream notice. The spring tuning,
// additive impulses, tanh soft saturation and inverse-tanh held target below are a
// direct behavioural port; only the storage is native.

#include <array>
#include <cstddef>
#include <span>

class JellyCatMotion {
public:
  static constexpr std::size_t kSpringCount = 8;
  static constexpr std::size_t kBandCount = 24;

  JellyCatMotion() = default;

  void reset() noexcept;

  // True while any spring still carries visible displacement or momentum.
  [[nodiscard]] bool active() const noexcept;

  // Softly saturated pose (limit * tanh(q / limit)), not the raw displacement.
  [[nodiscard]] std::array<float, 4> bodyPose() const noexcept;
  [[nodiscard]] std::array<float, 2> earPose() const noexcept;
  [[nodiscard]] std::array<float, 2> upperPose() const noexcept;
  [[nodiscard]] float squashPose() const noexcept;

  // Additive velocity impulses on the four modelled degrees of freedom.
  void excite(float x, float z, float squash, float twist) noexcept;
  // A pointer hit: lateral/depth/squash/twist derived from the normalized point.
  void poke(float x, float y, float yaw) noexcept;

  // Positive per-band flux against `previous`, rotated by the current flow phase.
  // `scale` is the user music sensitivity (1.0 reproduces the original impulse).
  // Returns true when impulses were applied. Rejects sizing/non-finite/out-of-range input.
  bool
  dance(std::span<const float> levels, std::span<const float> previous, float phase, float scale) noexcept;

  // Integrates the springs. `seconds` is clamped to 50 ms and substepped to <=4 ms.
  // Returns true while the motion is still awake.
  bool advance(float seconds, float squeeze, bool held) noexcept;

private:
  [[nodiscard]] float saturate(std::size_t index) const noexcept;

  std::array<float, kSpringCount> m_q{};
  std::array<float, kSpringCount> m_v{};
};
