#pragma once

#include "render/animation/animation_manager.h"
#include "render/scene/input_area.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"

#include <algorithm>
#include <memory>
#include <utility>

// Fixed layout footprint; hover only covers the stationary anchor and the
// currently revealed content. One InputArea prevents child-to-child flicker.
class SlidingReveal final : public InputArea {
public:
  SlidingReveal() {
    setAcceptedButtons(InputArea::buttonMask(BTN_LEFT));
    setOnEnter([this](const PointerData&) { setExpanded(true); });
    setOnLeave([this] { setExpanded(false); });
    setOnPress([this](const PointerData& data) {
      if (data.pressed)
        m_coverPressed = isCoverPoint(data.localX, data.localY);
    });
    setOnCancel([this] { m_coverPressed = false; });
    setOnClick([this](const PointerData& data) {
      const bool activate = m_coverPressed && isCoverPoint(data.localX, data.localY);
      m_coverPressed = false;
      if (activate && m_onCoverClick)
        m_onCoverClick();
    });
    auto content = std::make_unique<Box>();
    content->setFill(clearColorSpec());
    content->setClipChildren(true);
    m_content = static_cast<Box*>(addChild(std::move(content)));
    auto image = std::make_unique<Image>();
    image->setFit(ImageFit::Stretch);
    m_image = static_cast<Image*>(addChild(std::move(image)));
  }

  [[nodiscard]] Box* content() const noexcept { return m_content; }
  [[nodiscard]] Image* handleImage() const noexcept { return m_image; }
  [[nodiscard]] float revealWidth() const noexcept { return m_reveal; }

  void setCoverClickCallback(VoidCallback callback) { m_onCoverClick = std::move(callback); }
  void setCoverWidth(float width) { m_coverWidth = std::max(0.0F, width); }
  void setHoverBefore(float width) { m_hoverBefore = std::max(0.0F, width); }
  void setHandleWidth(float width) {
    const float previous = capacity();
    m_handleWidth = std::max(0.0F, width);
    syncGeometry();
    if (capacity() != previous)
      retarget();
  }
  void setSize(float width, float height) override {
    const float previous = capacity();
    InputArea::setSize(width, height);
    syncGeometry();
    if (capacity() != previous)
      retarget();
  }

protected:
  [[nodiscard]] bool containsLocalPoint(float x, float y, bool includeHitOutset) const override {
    const float left = includeHitOutset ? -m_hoverBefore : 0.0F;
    return x >= left && x < m_reveal + std::min(width(), m_handleWidth) && y >= 0.0F && y < height();
  }

private:
  [[nodiscard]] float capacity() const { return std::max(0.0F, width() - m_handleWidth); }
  [[nodiscard]] bool isCoverPoint(float x, float y) const {
    const float top = std::max(0.0F, (height() - m_coverWidth) * 0.5F);
    return x >= -m_coverWidth && x < 0.0F && y >= top && y < height() - top;
  }
  void setExpanded(bool expanded) {
    if (m_expanded == expanded)
      return;
    m_expanded = expanded;
    retarget();
  }
  void retarget() {
    auto* manager = animationManager();
    if (manager && m_animation != 0)
      manager->cancel(m_animation);
    m_animation = 0;
    const float target = m_expanded ? capacity() : 0.0F;
    if (target == m_reveal)
      return;
    if (manager) {
      m_animation = manager->animate(
          m_reveal, target, 220.0F, Easing::EaseOutCubic,
          [this](float value) {
            m_reveal = value;
            syncGeometry();
          },
          [this] { m_animation = 0; }, this
      );
      markPaintDirty();
    } else {
      m_reveal = target;
      syncGeometry();
    }
  }
  void syncGeometry() {
    m_reveal = std::clamp(m_reveal, 0.0F, capacity());
    m_content->setSize(m_reveal, height());
    m_content->setVisible(m_reveal > 0.0F);
    m_image->setPosition(m_reveal, 0.0F);
    m_image->setSize(std::min(width(), m_handleWidth), height());
  }
  void doLayout(Renderer& renderer) override {
    m_content->layout(renderer);
    m_image->layout(renderer);
    syncGeometry();
  }

  Box* m_content = nullptr;
  Image* m_image = nullptr;
  VoidCallback m_onCoverClick;
  AnimationManager::Id m_animation = 0;
  float m_handleWidth = 27.0F;
  float m_hoverBefore = 0.0F;
  float m_coverWidth = 40.0F;
  float m_reveal = 0.0F;
  bool m_expanded = false;
  bool m_coverPressed = false;
};
