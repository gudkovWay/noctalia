#include "shell/desktop/widgets/desktop_jelly_cat_widget.h"

#include "pipewire/pipewire_spectrum.h"
#include "render/scene/node.h"
#include "ui/builders.h"
#include "ui/visuals/jelly_cat.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace {

  constexpr float kDefaultCatSize = 210.0F;
  constexpr float kMinCatSize = 96.0F;
  constexpr float kMaxCatSize = 512.0F;
  constexpr float kDefaultSensitivity = 1.0F;
  constexpr float kMinSensitivity = 0.1F;
  constexpr float kMaxSensitivity = 3.0F;

} // namespace

DesktopJellyCatWidget::DesktopJellyCatWidget(PipeWireSpectrum* spectrum, Options options)
    : m_spectrum(spectrum), m_size(std::clamp(options.size, kMinCatSize, kMaxCatSize)),
      m_color(std::move(options.color)),
      m_sensitivity(std::clamp(options.sensitivity, kMinSensitivity, kMaxSensitivity)),
      m_musicEnabled(options.musicEnabled) {}

DesktopJellyCatWidget::~DesktopJellyCatWidget() { unsubscribeSpectrum(/*clearValues=*/false); }

void DesktopJellyCatWidget::create() {
  auto rootNode = ui::node({});

  auto cat = std::make_unique<JellyCat>();
  cat->setMilkColor(m_color);
  cat->setSensitivity(m_sensitivity);
  cat->setWakeCallback([this]() {
    requestFrameTick();
    requestRedraw();
  });
  m_cat = cat.get();
  rootNode->addChild(std::move(cat));

  setRoot(std::move(rootNode));

  if (m_musicEnabled) {
    subscribeSpectrum();
  }
}

bool DesktopJellyCatWidget::applySetting(
    const std::string& key, const WidgetSettingValue& value,
    const std::unordered_map<std::string, WidgetSettingValue>& allSettings, Renderer& renderer
) {
  if (key == "size") {
    float parsed = kDefaultCatSize;
    if (const auto* doubleValue = std::get_if<double>(&value)) {
      parsed = static_cast<float>(*doubleValue);
    } else if (const auto* integerValue = std::get_if<std::int64_t>(&value)) {
      parsed = static_cast<float>(*integerValue);
    } else {
      return false;
    }
    m_size = std::clamp(parsed, kMinCatSize, kMaxCatSize);
    requestLayout();
    return true;
  }
  if (key == "color") {
    if (const auto* v = std::get_if<std::string>(&value)) {
      m_color = colorSpecFromConfigString(*v, key);
      if (m_cat != nullptr) {
        m_cat->setMilkColor(m_color);
      }
      return true;
    }
    return false;
  }
  if (key == "sensitivity") {
    float parsed = kDefaultSensitivity;
    if (const auto* doubleValue = std::get_if<double>(&value)) {
      parsed = static_cast<float>(*doubleValue);
    } else if (const auto* integerValue = std::get_if<std::int64_t>(&value)) {
      parsed = static_cast<float>(*integerValue);
    } else {
      return false;
    }
    m_sensitivity = std::clamp(parsed, kMinSensitivity, kMaxSensitivity);
    if (m_cat != nullptr) {
      m_cat->setSensitivity(m_sensitivity);
    }
    return true;
  }
  if (key == "music_enabled") {
    if (const auto* v = std::get_if<bool>(&value)) {
      m_musicEnabled = *v;
      if (m_musicEnabled) {
        subscribeSpectrum();
      } else {
        unsubscribeSpectrum(/*clearValues=*/true);
      }
      return true;
    }
    return false;
  }
  return DesktopWidget::applySetting(key, value, allSettings, renderer);
}

bool DesktopJellyCatWidget::needsFrameTick() const {
  return m_pendingSpectrum || (m_cat != nullptr && m_cat->needsFrameTick());
}

void DesktopJellyCatWidget::onFrameTick(float deltaMs, Renderer& /*renderer*/) {
  if (m_cat == nullptr) {
    return;
  }
  if (m_pendingSpectrum) {
    pullSpectrumValues();
  }
  m_cat->tick(deltaMs);
}

void DesktopJellyCatWidget::subscribeSpectrum() {
  if (m_spectrum == nullptr || m_listenerId != 0) {
    return;
  }
  m_listenerId = m_spectrum->addChangeListener(kSpectrumBands, [this]() {
    m_pendingSpectrum = true;
    requestFrameTick();
  });
}

void DesktopJellyCatWidget::unsubscribeSpectrum(bool clearValues) {
  if (m_spectrum != nullptr && m_listenerId != 0) {
    m_spectrum->removeChangeListener(m_listenerId);
  }
  m_listenerId = 0;
  m_pendingSpectrum = false;
  if (clearValues && m_cat != nullptr) {
    m_cat->clearSpectrum();
  }
}

void DesktopJellyCatWidget::pullSpectrumValues() {
  m_pendingSpectrum = false;
  if (m_cat == nullptr || m_spectrum == nullptr || m_listenerId == 0) {
    return;
  }
  const auto& spectrumValues = m_spectrum->values(m_listenerId);
  if (spectrumValues.size() != static_cast<std::size_t>(kSpectrumBands) || m_spectrum->idle()) {
    // Audio unavailable or band mismatch: drop stale history so the cat returns to rest.
    m_cat->clearSpectrum();
    return;
  }
  m_cat->setValues(std::span<const float>(spectrumValues.data(), spectrumValues.size()));
}

void DesktopJellyCatWidget::doLayout(Renderer& renderer) {
  if (root() == nullptr || m_cat == nullptr) {
    return;
  }
  // Square natural size at the current scale; the base class aspect-fits this
  // into the host box, so no separate placement setting is needed.
  const float side = m_size * contentScale();
  m_cat->setPosition(0.0F, 0.0F);
  m_cat->setSize(side, side);
  root()->setSize(side, side);
  // Host layout only calls widget->layout; the control's own doLayout (InputArea
  // fill) must be driven from here.
  m_cat->layout(renderer);
}

void DesktopJellyCatWidget::doUpdate(Renderer& /*renderer*/) {
  if (m_pendingSpectrum) {
    pullSpectrumValues();
  }
}
