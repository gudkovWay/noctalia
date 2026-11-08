#include "shell/bar/widgets/volume_widget.h"

#include "config/config_types.h"
#include "i18n/i18n.h"
#include "pipewire/pipewire_service.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "system/easyeffects_service.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/slider.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

VolumeWidget::VolumeWidget(PipeWireService* audio, EasyEffectsService* easyEffects, Options options)
    : m_audio(audio), m_easyEffects(easyEffects), m_showLabel(options.showLabel), m_sliderMode(options.slider),
      m_sliderShowThumb(options.sliderShowThumb), m_maximumVolume(options.maximumVolume), m_target(options.device),
      m_muteColor(options.muteColor), m_glyphOverride(std::move(options.glyph)),
      m_muteGlyphOverride(std::move(options.muteGlyph)),
      m_effectsProfileGlyphs(std::move(options.effectsProfileGlyphs)),
      m_customImage(widget_custom_image::fromConfig(options.customImage, options.customImageColorize)),
      m_sliderFillSource(widget_custom_image::fromConfig(options.sliderFillImage, false)),
      m_hideWhenInactive(options.hideWhenInactive && options.device == VolumeWidgetTarget::Input) {}

void VolumeWidget::create() {
  auto area = ui::inputArea({});
  if (m_sliderMode) {
    area->addChild(ui::image({.out = &m_image, .fit = ImageFit::Stretch}));
    if (m_sliderFillSource.enabled()) {
      auto clip = ui::node({});
      clip->setClipChildren(true);
      clip->setHitTestVisible(false);
      clip->addChild(ui::image({.out = &m_sliderFillImage, .fit = ImageFit::Stretch}));
      m_sliderFillClip = area->addChild(std::move(clip));
    }
    auto slider = std::make_unique<Slider>();
    m_slider = slider.get();
    m_slider->setRange(0, m_maximumVolume);
    m_slider->setStep(1);
    m_slider->setThumbSize(m_contentScale);
    const auto clear = fixedColorSpec(rgba(0, 0, 0, 0));
    m_slider->setCustomPalette({.track = clear, .fill = clear, .thumb = clear, .focus = clear});
    m_slider->setOnValueChanged([this](double value) {
      if (m_audio != nullptr) {
        if (m_target == VolumeWidgetTarget::Input)
          m_audio->setMicVolume(static_cast<float>(value) / 100.0F);
        else
          m_audio->setVolume(static_cast<float>(value) / 100.0F);
      }
      syncSliderProgress();
      requestRedraw();
    });
    area->addChild(std::move(slider));
    if (m_sliderShowThumb) {
      auto thumb = ui::box({.out = &m_sliderThumb, .fill = fixedColorSpec(rgba(0.90F, 0.85F, 0.81F))});
      thumb->setHitTestVisible(false);
      area->addChild(std::move(thumb));
    }
    setRoot(std::move(area));
    return;
  }

  if (m_customImage.enabled()) {
    area->addChild(ui::image({.out = &m_image, .fit = ImageFit::Contain}));
  } else {
    area->addChild(
        ui::glyph({
            .out = &m_glyph,
            .glyph = glyphName(1.0F, false),
            .glyphSize = Style::baseGlyphSize * m_contentScale,
            .color = widgetIconColorOr(colorSpecFromRole(ColorRole::OnSurface)),
        })
    );
  }

  area->addChild(
      ui::label({
          .out = &m_label,
          .fontSize = Style::fontSizeBody * fontScale(),
          .fontWeight = labelFontWeight(),
          .fontFamily = labelFontFamily(),
          .visible = m_showLabel,
      })
  );

  setRoot(std::move(area));
}

void VolumeWidget::doLayout(Renderer& renderer, float containerWidth, float containerHeight) {
  auto* rootNode = root();
  if (m_sliderMode && rootNode != nullptr) {
    const float scale = m_contentScale;
    rootNode->setSize(83.0F * scale, 56.0F * scale);
    m_image->setSize(83.0F * scale, 46.0F * scale);
    m_image->setPosition(0, 5.0F * scale);
    if (m_customImage.enabled())
      m_image->setSourceFile(renderer, m_customImage.path, 192, true);
    if (m_sliderFillClip != nullptr) {
      m_sliderFillClip->setPosition(0, 5.0F * scale);
      m_sliderFillImage->setSize(83.0F * scale, 46.0F * scale);
      m_sliderFillImage->setSourceFile(renderer, m_sliderFillSource.path, 192, true);
    }
    m_slider->setSize(72.0F * scale, 56.0F * scale);
    m_slider->setControlHeight(56.0F * scale);
    m_slider->setPosition(6.0F * scale, 0);
    m_slider->layout(renderer);
    if (m_sliderThumb != nullptr)
      m_sliderThumb->setSize(4.0F * scale, 22.0F * scale);
    syncSliderState();
    syncSliderProgress();
    return;
  }
  if ((m_glyph == nullptr && m_image == nullptr) || m_label == nullptr || rootNode == nullptr) {
    return;
  }
  m_isVertical = containerHeight > containerWidth;
  syncState(renderer);

  const float iconW = m_image != nullptr ? m_image->width() : m_glyph->width();
  const float iconH = m_image != nullptr ? m_image->height() : m_glyph->height();
  if (m_label->visible()) {
    m_label->measure(renderer);
  }

  const bool labelVisible = m_label->visible();
  if (m_isVertical && labelVisible) {
    const float w = std::max(iconW, m_label->width());
    if (m_image != nullptr) {
      m_image->setPosition(std::round((w - iconW) * 0.5F), 0.0F);
    } else {
      m_glyph->setPosition(std::round((w - iconW) * 0.5F), 0.0F);
    }
    m_label->setPosition(std::round((w - m_label->width()) * 0.5F), iconH);
    rootNode->setSize(w, iconH + m_label->height());
  } else {
    const float h = labelVisible ? std::max(iconH, m_label->height()) : iconH;
    if (m_image != nullptr) {
      m_image->setPosition(0.0F, std::round((h - iconH) * 0.5F));
    } else {
      m_glyph->setPosition(0.0F, std::round((h - iconH) * 0.5F));
    }
    float totalWidth = iconW;
    if (labelVisible) {
      m_label->setPosition(iconW + Style::spaceXs, std::round((h - m_label->height()) * 0.5F));
      totalWidth = m_label->x() + m_label->width();
    }
    rootNode->setSize(totalWidth, h);
  }
}

void VolumeWidget::doUpdate(Renderer& renderer) { syncState(renderer); }

void VolumeWidget::syncWidgetVisibility(bool showWidget) {
  if (Node* rootNode = root(); rootNode != nullptr) {
    if (rootNode->visible() != showWidget || rootNode->participatesInLayout() != showWidget) {
      rootNode->setVisible(showWidget);
      rootNode->setParticipatesInLayout(showWidget);
      requestUpdate();
    }
  }
}

void VolumeWidget::syncSliderProgress() {
  if (m_slider == nullptr)
    return;
  const float value = std::clamp(static_cast<float>(m_slider->value() / m_maximumVolume), 0.0F, 1.0F);
  const float markerX = (5.0F + 71.0F * value) * m_contentScale;
  if (m_sliderThumb != nullptr)
    m_sliderThumb->setPosition(markerX, 17.0F * m_contentScale);
  if (m_sliderFillClip != nullptr) {
    m_sliderFillClip->setSize(markerX + 2.0F * m_contentScale, 46.0F * m_contentScale);
    m_sliderFillClip->setVisible(m_slider->enabled() && value > 0.0F);
  }
}

void VolumeWidget::syncSliderState() {
  const auto* node = m_audio == nullptr ? nullptr
      : m_target == VolumeWidgetTarget::Input ? m_audio->defaultSource() : m_audio->defaultSink();
  m_slider->setEnabled(node != nullptr);
  if (m_sliderThumb != nullptr)
    m_sliderThumb->setVisible(node != nullptr);
  if (m_sliderFillClip != nullptr) {
    m_sliderFillClip->setVisible(node != nullptr && m_slider->value() > 0.0);
  }
  if (node == nullptr || (node->volume == m_lastVolume && node->muted == m_lastMuted))
    return;
  m_lastVolume = node->volume;
  m_lastMuted = node->muted;
  if (!m_slider->dragging())
    m_slider->setValue(node->volume * 100.0);
  if (m_sliderThumb != nullptr)
    m_sliderThumb->setFill(node->muted ? m_muteColor : fixedColorSpec(rgba(0.90F, 0.85F, 0.81F)));
  syncSliderProgress();
  static_cast<InputArea*>(root())->setTooltip(
      std::to_string(static_cast<int>(std::round(node->volume * 100.0F))) + "% · " + audioDeviceLabel(*node));
  requestRedraw();
}

void VolumeWidget::syncState(Renderer& renderer) {
  if (m_sliderMode) {
    syncSliderState();
    return;
  }
  if (m_audio == nullptr || (m_glyph == nullptr && m_image == nullptr) || m_label == nullptr) {
    return;
  }

  const auto* node = m_target == VolumeWidgetTarget::Input ? m_audio->defaultSource() : m_audio->defaultSink();
  float volume = node != nullptr ? node->volume : 0.0F;
  bool muted = node != nullptr ? node->muted : false;
  const auto kind =
      m_target == VolumeWidgetTarget::Input ? AudioEffectsProfileKind::Input : AudioEffectsProfileKind::Output;
  const std::string effectsProfile =
      m_easyEffects != nullptr ? m_easyEffects->activeEffectsProfile(kind) : std::string{};
  // Reuses the privacy signal: a microphone capture exists while an application is linked to a
  // source. Skipped entirely for the output widget, where m_hideWhenInactive is always false.
  const bool micActive =
      m_hideWhenInactive && std::ranges::any_of(m_audio->privacyState().captures, [](const PrivacyCapture& capture) {
        return capture.kind == PrivacyCaptureKind::Microphone;
      });

  if (volume == m_lastVolume
      && muted == m_lastMuted
      && m_isVertical == m_lastVertical
      && effectsProfile == m_lastEffectsProfile
      && micActive == m_lastMicActive) {
    return;
  }

  m_lastVolume = volume;
  m_lastMuted = muted;
  m_lastVertical = m_isVertical;
  m_lastEffectsProfile = effectsProfile;
  m_lastMicActive = micActive;

  const bool showWidget = !m_hideWhenInactive || micActive;
  syncWidgetVisibility(showWidget);
  if (!showWidget) {
    return;
  }

  if (m_image != nullptr) {
    widget_custom_image::sync(
        *m_image, renderer, m_customImage, m_contentScale,
        muted ? m_muteColor : widgetIconColorOr(colorSpecFromRole(ColorRole::OnSurface))
    );
  } else {
    m_glyph->setGlyph(glyphName(volume, muted, effectsProfile));
    m_glyph->setGlyphSize(Style::baseGlyphSize * m_contentScale);
    m_glyph->setColor(muted ? m_muteColor : widgetIconColorOr(colorSpecFromRole(ColorRole::OnSurface)));
    m_glyph->measure(renderer);
  }

  m_label->setVisible(m_showLabel);
  if (m_showLabel) {
    int pct = static_cast<int>(std::round(volume * 100.0F));
    m_label->setFontSize((m_isVertical ? Style::fontSizeCaption : Style::fontSizeBody) * fontScale());
    m_label->setText(m_isVertical ? std::to_string(pct) : std::to_string(pct) + "%");
    m_label->setColor(muted ? m_muteColor : widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurface)));
    m_label->measure(renderer);
  }

  if (auto* rootNode = root(); rootNode != nullptr) {
    auto* area = static_cast<InputArea*>(rootNode);
    if (node != nullptr) {
      int pct = static_cast<int>(std::round(volume * 100.0F));
      std::vector<TooltipRow> rows;
      rows.push_back(
          {i18n::tr(m_target == VolumeWidgetTarget::Input ? "bar.widgets.volume.mic" : "bar.widgets.volume.volume"),
           std::to_string(pct) + "%"}
      );
      if (std::string deviceLabel = audioDeviceLabel(*node); !deviceLabel.empty()) {
        rows.push_back({i18n::tr("bar.widgets.volume.device"), std::move(deviceLabel), TextEllipsize::Middle});
      }
      if (!effectsProfile.empty()) {
        rows.push_back({i18n::tr("control-center.audio.effects-profile"), effectsProfile});
      }
      area->setTooltip(std::move(rows));
    } else {
      area->clearTooltip();
    }
  }

  requestRedraw();
}

std::string VolumeWidget::glyphName(float volume, bool muted, const std::string& effectsProfile) const {
  const char* dynamicGlyph = audioVolumeGlyph(volume, muted, m_target == VolumeWidgetTarget::Input);
  const std::string_view muteGlyph = m_target == VolumeWidgetTarget::Input ? "microphone-mute" : "volume-mute";
  const bool usingMuteGlyph = std::string_view{dynamicGlyph} == muteGlyph;
  if (usingMuteGlyph && !m_muteGlyphOverride.empty()) {
    return m_muteGlyphOverride;
  }
  if (!usingMuteGlyph && !effectsProfile.empty()) {
    const auto profileGlyph = m_effectsProfileGlyphs.find(effectsProfile);
    if (profileGlyph != m_effectsProfileGlyphs.end() && !profileGlyph->second.empty()) {
      return profileGlyph->second;
    }
  }
  if (!usingMuteGlyph && !m_glyphOverride.empty()) {
    return m_glyphOverride;
  }
  return dynamicGlyph;
}
