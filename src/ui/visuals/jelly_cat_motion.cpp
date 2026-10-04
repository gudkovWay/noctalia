#include "ui/visuals/jelly_cat_motion.h"

#include <algorithm>
#include <cmath>

namespace {

  // Direct ports of NekoMotion.js: sway X/Z, squash, twist, two ears and
  // delayed upper-body sway X/Z (tunable visual modes, not measured material).
  constexpr std::array<float, JellyCatMotion::kSpringCount> kStiffness = {225.0F, 196.0F, 400.0F, 256.0F,
                                                                          625.0F, 676.0F, 324.0F, 289.0F};
  constexpr std::array<float, JellyCatMotion::kSpringCount> kDamping = {7.5F, 7.0F, 8.0F, 8.0F,
                                                                        12.0F, 12.0F, 13.0F, 12.0F};
  constexpr std::array<float, JellyCatMotion::kSpringCount> kLimits = {0.38F, 0.33F, 0.34F, 0.32F,
                                                                       0.26F, 0.26F, 0.42F, 0.37F};

  // The squash spring is stiffer under compression than under stretch.
  constexpr float kSquashNegativeLimit = 0.20F;
  constexpr float kImpulseVelocityLimit = 10.0F;
  constexpr float kHeldDrag = 28.0F;
  constexpr float kMaxStepSeconds = 0.004F;
  constexpr float kMaxElapsedSeconds = 0.05F;
  constexpr float kAwakeDisplacement = 0.0005F;
  constexpr float kAwakeVelocity = 0.008F;

} // namespace

void JellyCatMotion::reset() noexcept {
  m_q.fill(0.0F);
  m_v.fill(0.0F);
}

float JellyCatMotion::saturate(std::size_t index) const noexcept {
  const float limit = index == 2 && m_q[index] < 0.0F ? kSquashNegativeLimit : kLimits[index];
  return limit * std::tanh(m_q[index] / limit);
}

std::array<float, 4> JellyCatMotion::bodyPose() const noexcept {
  return {saturate(0), saturate(1), saturate(2), saturate(3)};
}

std::array<float, 2> JellyCatMotion::earPose() const noexcept { return {saturate(4), saturate(5)}; }

std::array<float, 2> JellyCatMotion::upperPose() const noexcept { return {saturate(6), saturate(7)}; }

float JellyCatMotion::squashPose() const noexcept { return saturate(2); }

void JellyCatMotion::excite(float x, float z, float squash, float twist) noexcept {
  if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(squash) || !std::isfinite(twist)) {
    return;
  }
  const std::array<float, 4> impulse = {x, z, squash, twist};
  for (std::size_t i = 0; i < impulse.size(); ++i) {
    m_v[i] = std::clamp(m_v[i] + impulse[i], -kImpulseVelocityLimit, kImpulseVelocityLimit);
  }
}

void JellyCatMotion::poke(float x, float y, float yaw) noexcept {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(yaw)) {
    return;
  }
  const float side = std::clamp(x * 2.0F - 1.0F, -1.0F, 1.0F);
  const float height = std::clamp(1.0F - y, 0.0F, 1.0F);
  const float lateral = -side * (2.0F + height * 2.2F);
  const float depth = (0.3F - height) * 2.8F;
  // Hit the main mass; the upper body and ears follow through their springs.
  excite(
      lateral * std::cos(yaw) + depth * std::sin(yaw),
      -lateral * std::sin(yaw) + depth * std::cos(yaw),
      3.0F + (1.0F - height) * 1.6F,
      side * (height - 0.5F) * 2.2F
  );
}

bool JellyCatMotion::dance(
    std::span<const float> levels, std::span<const float> previous, float phase, float scale
) noexcept {
  if (!std::isfinite(phase) || levels.size() != kBandCount) {
    return false;
  }
  for (const float level : levels) {
    if (!std::isfinite(level) || level < 0.0F || level > 1.0F) {
      return false;
    }
  }

  const bool haveHistory = previous.size() == kBandCount;
  float flux = 0.0F;
  float peakRise = 0.0F;
  // Per-band rises catch drums, vocals and cymbals even when total loudness
  // stays constant. A shorter history reads as silence, matching the original.
  for (std::size_t i = 0; i < kBandCount; ++i) {
    const float current = std::max(0.0F, levels[i] - 0.03F);
    float beforeRaw = 0.0F;
    if (haveHistory && std::isfinite(previous[i])) {
      beforeRaw = previous[i];
    }
    const float before = std::max(0.0F, beforeRaw - 0.03F);
    const float rise = std::max(0.0F, current - before);
    flux += rise;
    peakRise = std::max(peakRise, rise);
  }

  const float strength =
      std::clamp(flux / static_cast<float>(kBandCount) * 8.0F + peakRise * 0.6F, 0.0F, 1.0F);
  if (strength <= 0.0F) {
    return false;
  }

  const float appliedScale = std::max(0.0F, scale);
  // Two perpendicular components avoid a dead spot when one sine is zero.
  const float side = std::cos(phase * 2.0F);
  const float depth = std::sin(phase * 2.0F);
  excite(
      side * strength * 4.0F * appliedScale,
      depth * strength * 3.0F * appliedScale,
      strength * 1.6F * appliedScale,
      side * strength * 0.6F * appliedScale
  );
  return true;
}

bool JellyCatMotion::advance(float seconds, float squeeze, bool held) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0F) {
    return active();
  }

  // Discard suspend/stall time; substepping keeps rapid repeated pokes bounded.
  const float dt = std::min(seconds, kMaxElapsedSeconds);
  const int steps = std::max(1, static_cast<int>(std::ceil(dt / kMaxStepSeconds)));
  const float h = dt / static_cast<float>(steps);

  // The finger only moves the held spring target; inverse tanh keeps the
  // requested visible compression independent of the spring's soft saturation.
  const float target = held && std::isfinite(squeeze) ? std::clamp(squeeze, -0.16F, 0.28F) : 0.0F;
  const float limit = target < 0.0F ? kSquashNegativeLimit : kLimits[2];
  const float heldSquash = limit * std::atanh(target / limit);

  for (int step = 0; step < steps; ++step) {
    // Recalculated every substep so the upper body and ears follow the newly
    // integrated main mass, exactly as the original does.
    const std::array<float, JellyCatMotion::kSpringCount> targets = {
        0.0F,
        0.0F,
        heldSquash,
        0.0F,
        m_q[6] * 0.45F + m_q[7] * 0.30F + m_q[3] * 0.55F - m_q[2] * 0.25F,
        m_q[6] * 0.45F + m_q[7] * 0.30F - m_q[3] * 0.55F + m_q[2] * 0.25F,
        m_q[0],
        m_q[1] - m_q[2] * 0.45F,
    };
    for (std::size_t i = 0; i < JellyCatMotion::kSpringCount; ++i) {
      const float drag = held && i == 2 ? kHeldDrag : kDamping[i];
      m_v[i] += (kStiffness[i] * (targets[i] - m_q[i]) - drag * m_v[i]) * h;
      m_q[i] += m_v[i] * h;
    }
  }

  if (!active()) {
    m_q.fill(0.0F);
    m_v.fill(0.0F);
    return false;
  }
  return true;
}

bool JellyCatMotion::active() const noexcept {
  for (std::size_t i = 0; i < kSpringCount; ++i) {
    if (std::fabs(m_q[i]) > kAwakeDisplacement || std::fabs(m_v[i]) > kAwakeVelocity) {
      return true;
    }
  }
  return false;
}
