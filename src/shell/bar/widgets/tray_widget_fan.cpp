#include "render/scene/image_node.h"
#include "render/scene/input_area.h"
#include "shell/bar/widgets/tray_widget.h"
#include "shell/panel/panel_manager.h"
#include "shell/tray/tray_fan_art.h"
#include "shell/tray/tray_fan_geometry.h"
#include "shell/tray/tray_identifier.h"
#include "ui/builders.h"
#include "util/base64.h"

#include <array>
#include <linux/input-event-codes.h>

namespace {
  class FanSectorArea final : public InputArea {
  public:
    FanSectorArea(tray::FanLayout layout, std::size_t slot) : m_layout(layout), m_slot(slot) {}

  protected:
    bool containsLocalPoint(float x, float y, bool) const override { return m_layout.sectorAt(x, y) == m_slot; }

  private:
    tray::FanLayout m_layout;
    std::size_t m_slot;
  };

  // The invisible texture owner precedes these nodes in the scene traversal:
  // renderer rebinding and GPU reset reload it once, then refresh every reference.
  class FanLeafNode final : public ImageNode {
  public:
    FanLeafNode(Image& owner, ColorRole role) : m_owner(owner), m_role(role) {
      refreshTexture();
      applyPalette();
    }
    void applyPalette() {
      const Color base = colorForRole(m_role);
      setTint(m_hovered ? lerpColor(base, colorForRole(ColorRole::Primary), 0.12F) : base);
    }
    void setHovered(bool hovered) {
      m_hovered = hovered;
      applyPalette();
    }

  protected:
    void doInvalidateGpuResources(Renderer&) override { refreshTexture(); }

  private:
    void refreshTexture() {
      setTextureId(m_owner.textureId());
      setTextureSize(m_owner.sourceWidth(), m_owner.sourceHeight());
    }
    Image& m_owner;
    ColorRole m_role;
    bool m_hovered = false;
  };

  const std::string& fanButtonSource() {
    static const std::string value = "data:image/svg+xml;base64," + Base64::encode(tray::kFanButtonSvg);
    return value;
  }
  const std::string& fanPaperSource(std::size_t leaves) {
    static std::array<std::string, tray::kFanPageSize> sources;
    auto& source = sources[leaves - 1];
    if (source.empty())
      source = "data:image/svg+xml;base64," + Base64::encode(tray::fanPaperSvg(leaves));
    return source;
  }
  const std::string& fanRibsSource() {
    static const std::string value = "data:image/svg+xml;base64," + Base64::encode(tray::kFanRibsSvg);
    return value;
  }
  const std::string& fanOutlineSource() {
    static const std::string value = "data:image/svg+xml;base64," + Base64::encode(tray::kFanOutlineSvg);
    return value;
  }
  const std::string& fanPivotSource() {
    static const std::string value = "data:image/svg+xml;base64," + Base64::encode(tray::kFanPivotSvg);
    return value;
  }
} // namespace

void TrayWidget::buildFanScene(Renderer& renderer) {
  if (m_fanOpener) {
    const float scale = m_contentScale;
    auto button = ui::inputArea({});
    auto* buttonPtr = button.get();
    button->setSize(44.0F * scale, 35.0F * scale);
    button->setTooltip("Tray");
    button->setOnClick([this, buttonPtr](const InputArea::PointerData& data) {
      if (data.button != BTN_LEFT)
        return;
      float x = 0, y = 0;
      Node::absolutePosition(buttonPtr, x, y);
      requestPanelToggle(
          "tray-drawer", configName(), x + buttonPtr->width() * 0.5F, y + buttonPtr->height() + 4.0F * m_contentScale
      );
    });
    auto icon = ui::image({.width = 30.0F * scale, .height = 24.0F * scale});
    (void)icon->setSourceFile(renderer, fanButtonSource(), 80, true);
    icon->setPosition(7.0F * scale, 5.0F * scale);
    m_loadedImages.push_back(icon.get());
    button->addChild(std::move(icon));
    m_container->addChild(std::move(button));
    return;
  }

  m_fanItemCount = 0;
  for (const auto& item : m_items) {
    if (!(m_hidePassive && tray::isPassiveStatus(item)) && !isHiddenItem(item))
      ++m_fanItemCount;
  }
  if (m_fanExtent <= 0)
    m_fanExtent = tray::kFanExtent * m_contentScale;
  const tray::FanLayout layout(m_fanItemCount, m_fanPage, m_fanExtent);
  m_fanPage = layout.page();
  const float scale = layout.scale();

  // Transparent corners dismiss the panel, never activate a sector.
  auto outside = ui::inputArea({});
  outside->setSize(m_fanExtent, m_fanExtent);
  outside->setParticipatesInLayout(false);
  outside->setOnClick([](const InputArea::PointerData&) { PanelManager::instance().close(); });
  outside->setOnAxis([this](const InputArea::PointerData& data) {
    if (data.scrollSteps() != 0)
      changeFanPage(data.scrollSteps() < 0 ? -1 : 1);
  });
  m_container->addChild(std::move(outside));

  // Two neutral shared textures; palette changes update uniforms, never rasterize SVG.
  if (!m_fanLeafTexture) {
    auto textureOwner = ui::image({.visible = false, .participatesInLayout = false});
    m_fanLeafTexture = static_cast<Image*>(m_container->insertChildAt(0, std::move(textureOwner)));
  }
  if (!m_fanRibTexture) {
    auto textureOwner = ui::image({.visible = false, .participatesInLayout = false});
    m_fanRibTexture = static_cast<Image*>(m_container->insertChildAt(0, std::move(textureOwner)));
  }
  const auto leaves = std::max<std::size_t>(1, layout.count());
  const int textureSize = static_cast<int>(layout.radius() * 2.0F);
  (void)m_fanLeafTexture->setSourceFile(renderer, fanPaperSource(leaves), textureSize, true);
  (void)m_fanRibTexture->setSourceFile(renderer, fanRibsSource(), textureSize, true);
  m_fanLeaves.reserve(leaves + 1);
  m_fanRibs.reserve(leaves + 1);
  for (std::size_t i = 0; i <= leaves; ++i) {
    auto leaf = std::make_unique<FanLeafNode>(*m_fanLeafTexture, ColorRole::Surface);
    leaf->setHitTestVisible(false);
    leaf->setParticipatesInLayout(false);
    auto ribs = std::make_unique<FanLeafNode>(*m_fanRibTexture, ColorRole::Outline);
    ribs->setHitTestVisible(false);
    ribs->setParticipatesInLayout(false);
    m_fanRibs.push_back(ribs.get());
    leaf->addChild(std::move(ribs));
    m_fanLeaves.push_back(leaf.get());
    m_container->addChild(std::move(leaf));
  }

  auto outline = ui::image({.participatesInLayout = false});
  (void)outline->setSourceFile(renderer, fanOutlineSource(), static_cast<int>(m_fanExtent * 2.0F), true);
  outline->setHitTestVisible(false);
  m_fanOutline = outline.get();
  m_loadedImages.push_back(outline.get());
  m_container->addChild(std::move(outline));
  refreshFanPalette();

  auto content = ui::inputArea({.width = m_fanExtent, .height = m_fanExtent, .participatesInLayout = false});
  content->setAcceptedButtons(0);
  content->setHitTestSelf(false);
  content->setOnAxis([this](const InputArea::PointerData& data) {
    if (data.scrollSteps() != 0)
      changeFanPage(data.scrollSteps() < 0 ? -1 : 1);
  });
  m_fanContent = m_container->addChild(std::move(content));
  if (m_fanItemCount == 0) {
    auto label = ui::label(
        {.text = "No tray applications",
         .fontSize = 13.0F * scale,
         .color = colorSpecFromRole(ColorRole::OnSurface),
         .width = 144.0F * scale,
         .textAlign = TextAlign::Center}
    );
    label->measure(renderer);
    label->setPosition(48.0F * scale, 105.0F * scale);
    m_fanContent->addChild(std::move(label));
  }
  if (layout.pages() > 1) {
    for (int direction : {-1, 1}) {
      auto page = ui::inputArea({});
      page->setSize(32.0F * scale, 32.0F * scale);
      page->setPosition(layout.pivot().x - (direction < 0 ? 108.0F : 36.0F) * scale, 58.0F * scale);
      page->setTooltip(direction < 0 ? "Previous applications" : "Next applications");
      page->setOnClick([this, direction](const InputArea::PointerData&) { changeFanPage(direction); });
      auto glyph = ui::glyph(
          {.glyph = direction < 0 ? "chevron-left" : "chevron-right",
           .glyphSize = 16.0F * scale,
           .color = colorSpecFromRole(ColorRole::OnSurface)}
      );
      glyph->measure(renderer);
      glyph->setPosition(8.0F * scale, 8.0F * scale);
      page->addChild(std::move(glyph));
      m_fanInputs.push_back(page.get());
      page->setZIndex(10);
      m_fanContent->addChild(std::move(page));
    }
    auto counter = ui::label(
        {.text = std::to_string(layout.page() + 1) + " / " + std::to_string(layout.pages()),
         .fontSize = 11.0F * scale,
         .color = colorSpecFromRole(ColorRole::OnSurface),
         .width = 40.0F * scale,
         .textAlign = TextAlign::Center}
    );
    counter->measure(renderer);
    counter->setZIndex(10);
    counter->setHitTestVisible(false);
    counter->setPosition(layout.pivot().x - 76.0F * scale, 67.0F * scale);
    m_fanContent->addChild(std::move(counter));
  }
  auto pivot = ui::image({.width = 24.0F * scale, .height = 75.0F * scale, .participatesInLayout = false});
  (void)pivot->setSourceFile(renderer, fanPivotSource(), 200, true);
  pivot->setHitTestVisible(false);
  m_loadedImages.push_back(pivot.get());
  m_fanPivot = m_container->addChild(std::move(pivot));
}

std::unique_ptr<InputArea> TrayWidget::createFanArea(std::size_t slot) {
  auto area = std::make_unique<FanSectorArea>(tray::FanLayout(m_fanItemCount, m_fanPage, m_fanExtent), slot);
  area->setSize(m_fanExtent, m_fanExtent);
  area->setParticipatesInLayout(false);
  area->setClipChildren(true);
  auto* leaf = static_cast<FanLeafNode*>(m_fanLeaves[slot]);
  area->setOnEnter([this, leaf](const InputArea::PointerData&) {
    leaf->setHovered(true);
    requestRedraw();
  });
  area->setOnLeave([this, leaf]() {
    leaf->setHovered(false);
    requestRedraw();
  });
  return area;
}

void TrayWidget::refreshFanPalette() {
  for (auto* leaf : m_fanLeaves)
    static_cast<FanLeafNode*>(leaf)->applyPalette();
  for (auto* ribs : m_fanRibs)
    static_cast<FanLeafNode*>(ribs)->applyPalette();
  if (m_fanOutline) {
    const auto& theme = colorForRole(ColorRole::OnSurfaceVariant);
    const float gray = 0.2126F * theme.r + 0.7152F * theme.g + 0.0722F * theme.b;
    m_fanOutline->setForegroundTint(fixedColorSpec(rgba(gray, gray, gray, theme.a)));
  }
}

void TrayWidget::changeFanPage(int direction) {
  if (!tray::fanActionsEnabled(m_fanProgress))
    return;
  const tray::FanLayout layout(m_fanItemCount, m_fanPage, m_fanExtent);
  const auto next =
      direction < 0 ? (layout.page() + layout.pages() - 1) % layout.pages() : (layout.page() + 1) % layout.pages();
  if (next == m_fanPage)
    return;
  m_fanPage = next;
  m_rebuildPending = true;
  requestUpdate();
}

void TrayWidget::layoutFanScene(float extent) {
  const tray::FanLayout layout(m_fanItemCount, m_fanPage, extent);
  const float radius = layout.radius();
  m_container->setSize(extent, extent);
  for (std::size_t i = 0; i < m_fanLeaves.size(); ++i) {
    auto* leaf = m_fanLeaves[i];
    leaf->setPosition(tray::kFanInset * layout.scale(), tray::kFanInset * layout.scale());
    leaf->setFrameSize(radius, radius);
    leaf->setTransformOrigin(radius, 0);
    m_fanRibs[i]->setFrameSize(radius, radius);
  }
  if (m_fanOutline) {
    // SVG radius is 400 with an 8px gutter on every side.
    const float gutter = radius * (8.0F / 400.0F);
    const float origin = tray::kFanInset * layout.scale() - gutter;
    m_fanOutline->setPosition(origin, origin);
    m_fanOutline->setSize(radius + 2.0F * gutter, radius + 2.0F * gutter);
  }
  if (m_fanPivot)
    m_fanPivot->setPosition(layout.pivot().x - 12.0F * layout.scale(), 0);
  setFanReveal(m_fanProgress);
}

void TrayWidget::setFanReveal(float progress) {
  m_fanProgress = std::clamp(progress, 0.0F, 1.0F);
  const auto leaves = m_fanLeaves.empty() ? 1U : m_fanLeaves.size() - 1;
  for (std::size_t i = 0; i < m_fanLeaves.size(); ++i) {
    m_fanLeaves[i]->setRotation(tray::fanLeafAngle(i, m_fanProgress, leaves));
    // Last leaf is the outer closing rib; its paper remains folded.
    m_fanLeaves[i]->setScale(1, i == leaves ? 0.015F : std::max(0.015F, m_fanProgress));
  }
  if (m_fanContent)
    m_fanContent->setOpacity(tray::fanContentOpacity(m_fanProgress));
  if (m_fanOutline)
    m_fanOutline->setOpacity(tray::fanContentOpacity(m_fanProgress));
  for (auto* area : m_fanInputs)
    area->setEnabled(tray::fanActionsEnabled(m_fanProgress));
}
