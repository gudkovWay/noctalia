#include "ui/visuals/jelly_cat.h"

#include "render/core/color.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "ui/palette.h"

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include <memory>

namespace {

  // MusicWidget.qml smoothing time constants (ms) and flow pacing.
  constexpr float kEnergyTauMs = 180.0F;
  constexpr float kBassFloorTauMs = 650.0F;
  constexpr float kPulseTauMs = 120.0F;
  constexpr float kFlowTauMs = 80.0F;
  constexpr float kTintTauMs = 300.0F;
  constexpr float kFlowMaxStepSeconds = 0.1F;
  constexpr float kFlowAudibleEnergy = 0.08F;
  constexpr float kTintActiveEnergy = 0.02F;
  constexpr float kChannelEpsilon = 1.0F / 512.0F;

} // namespace

JellyCat::JellyCat() : JellyCatNode() {
  auto input = std::make_unique<InputArea>();
  input->setAcceptedButtons(InputArea::buttonMask({BTN_LEFT, BTN_RIGHT}));
  input->setFocusable(false);
  input->setTabStop(false);
  input->setParticipatesInLayout(false);
  input->setOnPress([this](const InputArea::PointerData& data) { onPointerPress(data); });
  input->setOnMotion([this](const InputArea::PointerData& data) { onPointerMotion(data); });
  input->setOnCancel([this] { onPointerCancel(); });
  m_inputArea = static_cast<InputArea*>(addChild(std::move(input)));

  m_paletteConn = paletteChanged().connect([this] { refreshMilk(); });
  m_milkBase = resolveColorSpec(m_milkColorSpec);
  m_displayMilk = m_milkBase;
  pushStyle();
}

JellyCat::~JellyCat() = default;

void JellyCat::setMilkColor(const ColorSpec& color) {
  if (m_milkColorSpec == color) {
    return;
  }
  m_milkColorSpec = color;
  refreshMilk();
}

void JellyCat::setSensitivity(float sensitivity) {
  if (!std::isfinite(sensitivity)) {
    return;
  }
  m_sensitivity = std::clamp(sensitivity, 0.1F, 3.0F);
}

void JellyCat::setValues(std::span<const float> values) {
  if (values.size() != kBandCount) {
    clearSpectrum();
    return;
  }
  for (std::size_t i = 0; i < kBandCount; ++i) {
    const float value = values[i];
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
      clearSpectrum();
      return;
    }
    m_levels[i] = value;
  }

  m_audioPresent = true;
  m_rawEnergy = *std::max_element(m_levels.begin(), m_levels.end());
  float bass = 0.0F;
  for (std::size_t i = 2; i < 9; ++i) {
    bass = std::max(bass, m_levels[i]);
  }
  m_bass = bass;
  m_energyTarget = std::max(0.0F, (m_rawEnergy - kFlowAudibleEnergy) / 0.92F);

  if (!m_historyArmed) {
    // First frame after construction or a clear is a baseline only, so a stale
    // spectrum cannot inject energy.
    m_previousLevels = m_levels;
    m_historyArmed = true;
    if (m_energyTarget > kTintActiveEnergy) {
      requestWake();
    }
    return;
  }

  // A drag suppresses dance so impulses never fight the pointer; history still
  // advances so a release does not replay the drag as a sudden burst.
  const bool danced =
      !m_pressed && m_motion.dance(m_levels, m_previousLevels, m_flowPhase, m_sensitivity);
  m_previousLevels = m_levels;
  if (danced || m_energyTarget > kTintActiveEnergy) {
    requestWake();
  }
}

void JellyCat::clearSpectrum() {
  m_levels.fill(0.0F);
  m_previousLevels.fill(0.0F);
  m_historyArmed = false;
  m_audioPresent = false;
  m_rawEnergy = 0.0F;
  m_bass = 0.0F;
  m_energyTarget = 0.0F;
  // Re-arm the tint return before waking: a settled tinted pose has
  // m_tintAnimating == false, and with audio gone nothing else would request
  // frames, leaving the cat stuck on the last music colour.
  m_tintAnimating = m_displayMilk != m_milkBase;
  // Springs keep their momentum and settle through normal integration; only the
  // audio targets and history are dropped.
  requestWake();
}

bool JellyCat::needsFrameTick() const noexcept {
  return m_motion.active() || m_tintAnimating || audioAnimating();
}

void JellyCat::tick(float deltaMs) {
  const float ms = std::isfinite(deltaMs) ? std::clamp(deltaMs, 0.0F, 50.0F) : 0.0F;
  const float dt = ms * 0.001F;
  m_motion.advance(dt, m_squeeze, m_squeezing);
  updateAudio(dt);
  updateTint(dt);
  pushStyle();
}

void JellyCat::setWakeCallback(std::function<void()> callback) { m_wakeCallback = std::move(callback); }

void JellyCat::resetMotion() {
  m_motion.reset();
  m_squeezing = false;
  m_squeeze = 0.0F;
  m_pressed = false;
  m_dragging = false;
  m_holdingLeft = false;
  m_holdingRight = false;
  pushStyle();
}

void JellyCat::doLayout(Renderer& renderer) {
  if (m_inputArea != nullptr) {
    m_inputArea->setPosition(0.0F, 0.0F);
    m_inputArea->setFrameSize(width(), height());
  }
  JellyCatNode::doLayout(renderer);
}

void JellyCat::onPointerPress(const InputArea::PointerData& data) {
  const bool left = data.button == BTN_LEFT;
  const bool right = data.button == BTN_RIGHT;
  if (!left && !right) {
    return;
  }

  if (data.pressed) {
    const bool wasHolding = m_holdingLeft || m_holdingRight;
    (left ? m_holdingLeft : m_holdingRight) = true;
    // A second accepted button pressed mid-hold joins the ambient gesture but
    // does not restart the poke/hold.
    if (wasHolding) {
      return;
    }
    beginHold(data);
    return;
  }

  (left ? m_holdingLeft : m_holdingRight) = false;
  // The opposite button still held keeps the interaction alive, matching the
  // original widget's accepted-button release guard.
  if (m_holdingLeft || m_holdingRight) {
    return;
  }
  endHold();
}

void JellyCat::onPointerMotion(const InputArea::PointerData& data) {
  if (!m_pressed) {
    return;
  }
  const float w = std::max(1.0F, width());
  float dx = (data.localX - m_previousX) / w;
  m_previousX = data.localX;
  if (!m_dragging) {
    const float dxPress = data.localX - m_pressX;
    const float dyPress = data.localY - m_pressY;
    if (std::hypot(dxPress, dyPress) < 4.0F) {
      return;
    }
    dx = dxPress / w;
    m_dragging = true;
  }

  const float turned = m_yaw - dx * 5.2F;
  m_yaw = std::atan2(std::sin(turned), std::cos(turned));
  const float h = std::max(1.0F, height());
  m_squeeze = std::clamp(m_pressSquash + (data.localY - m_pressY) / h * 0.9F, -0.16F, 0.28F);
  // Vertical drag only moves the held spring target; extra squash impulses
  // would fight the pointer and make release unpredictable.
  excite(dx * kPointerExciteScale, 0.0F, 0.0F, -dx * kPointerTwistScale);
  requestWake();
}

void JellyCat::onPointerCancel() {
  m_holdingLeft = false;
  m_holdingRight = false;
  endHold();
}

void JellyCat::beginHold(const InputArea::PointerData& data) {
  m_pressed = true;
  m_pressX = m_previousX = data.localX;
  m_pressY = data.localY;
  m_pressSquash = std::clamp(m_motion.squashPose(), 0.10F, 0.28F);
  m_squeeze = m_pressSquash;
  m_squeezing = true;
  m_dragging = false;
  poke(data.localX / std::max(1.0F, width()), data.localY / std::max(1.0F, height()));
  requestWake();
}

void JellyCat::endHold() {
  m_squeezing = false;
  m_squeeze = 0.0F;
  m_dragging = false;
  m_pressed = false;
  requestWake();
}

void JellyCat::poke(float x, float y) { m_motion.poke(x, y, m_yaw); }

void JellyCat::excite(float x, float z, float squash, float twist) {
  const float c = std::cos(m_yaw);
  const float s = std::sin(m_yaw);
  m_motion.excite(x * c + z * s, -x * s + z * c, squash, twist);
}

void JellyCat::requestWake() {
  if (m_wakeCallback) {
    m_wakeCallback();
  }
}

void JellyCat::refreshMilk() {
  m_milkBase = resolveColorSpec(m_milkColorSpec);
  const Color target = milkTargetColor();
  if (!m_tintAnimating) {
    m_displayMilk = target;
  }
  pushStyle();
  if (m_tintAnimating) {
    requestWake();
  }
}

void JellyCat::pushStyle() {
  JellyCatStyle next = style();
  next.bodyMotion = m_motion.bodyPose();
  next.earMotion = m_motion.earPose();
  next.upperMotion = m_motion.upperPose();
  next.milkColor = m_displayMilk;
  next.yaw = m_yaw;
  next.pitch = kPitch;
  setStyle(next);
}

Color JellyCat::milkTargetColor() const {
  if (!m_audioPresent || m_energy <= kTintActiveEnergy) {
    return m_milkBase;
  }
  // Travelling palette: hue offset 0.5 as in MusicWidget.spectrumColor(0.5).
  const float hueTurns = 0.54F + 0.36F * (0.5F + 0.5F * std::sin(m_flowPhase - 1.2F));
  const Color tint = hsl(hueTurns * 360.0F, 0.45F + m_energy * 0.18F, 0.72F + m_pulse * 0.06F, 1.0F);
  const float blend = std::min(0.80F, 0.38F + m_energy * 0.40F + m_pulse * 0.25F);
  return Color{
      .r = m_milkBase.r + (tint.r - m_milkBase.r) * blend,
      .g = m_milkBase.g + (tint.g - m_milkBase.g) * blend,
      .b = m_milkBase.b + (tint.b - m_milkBase.b) * blend,
      .a = m_milkBase.a,
  };
}

void JellyCat::updateAudio(float dt) {
  if (dt <= 0.0F) {
    return;
  }
  const auto approach = [dt](float current, float target, float tauMs) {
    const float k = 1.0F - std::exp(-dt * 1000.0F / tauMs);
    return current + (target - current) * k;
  };

  m_energy = approach(m_energy, m_energyTarget, kEnergyTauMs);
  m_bassFloor = approach(m_bassFloor, m_bass, kBassFloorTauMs);
  const float pulseTarget = std::clamp((m_bass - m_bassFloor) * 4.0F, 0.0F, 1.0F);
  m_pulse = approach(m_pulse, pulseTarget, kPulseTauMs);

  if (m_audioPresent && m_rawEnergy > kFlowAudibleEnergy) {
    m_flowTarget +=
        std::min(kFlowMaxStepSeconds, dt) * (0.25F + m_energy * 1.4F + m_pulse * 1.8F);
  }
  m_flowPhase = approach(m_flowPhase, m_flowTarget, kFlowTauMs);
}

void JellyCat::updateTint(float dt) {
  const Color target = milkTargetColor();
  if (dt > 0.0F) {
    const float k = 1.0F - std::exp(-dt * 1000.0F / kTintTauMs);
    const auto channel = [k](float current, float goal) {
      const float next = current + (goal - current) * k;
      return std::fabs(goal - next) < kChannelEpsilon ? goal : next;
    };
    m_displayMilk = Color{
        .r = channel(m_displayMilk.r, target.r),
        .g = channel(m_displayMilk.g, target.g),
        .b = channel(m_displayMilk.b, target.b),
        .a = channel(m_displayMilk.a, target.a),
    };
  } else if (!m_tintAnimating) {
    m_displayMilk = target;
  }
  m_tintAnimating = m_displayMilk != target;
}

bool JellyCat::audioAnimating() const noexcept {
  return m_audioPresent && std::max(m_energy, m_energyTarget) > kTintActiveEnergy;
}
