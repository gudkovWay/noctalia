#include "ui/controls/box.h"

#include "render/core/render_styles.h"
#include "render/scene/rect_node.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <memory>

Box::Box() {
  auto rect = std::make_unique<RectNode>();
  m_rect = static_cast<RectNode*>(addChild(std::move(rect)));
  m_style = m_rect->style();
  m_paletteConn = paletteChanged().connect([this] { applyPalette(); });
}

const RoundedRectStyle& Box::style() const noexcept { return m_style; }

void Box::setStyle(const RoundedRectStyle& style) {
  m_style = style;
  m_resolveFill = false;
  m_resolveBorder = false;
  m_borderWidth = style.borderWidth;
  // An explicit full style owns the silhouette; stop tracking the profile.
  m_presentationChrome = false;
  m_presentationNormalCaptured = false;
  syncStyle();
}

void Box::setFill(const ColorSpec& color) {
  m_fill = color;
  m_resolveFill = true;
  applyPalette();
}

void Box::setFill(const Color& color) { setFill(fixedColorSpec(color)); }

void Box::setBorder(const ColorSpec& color, float width) {
  m_border = color;
  m_borderWidth = width;
  m_resolveBorder = true;
  applyPalette();
}

void Box::setBorder(const Color& color, float width) { setBorder(fixedColorSpec(color), width); }

void Box::clearBorder() {
  m_border = clearColorSpec();
  m_borderWidth = 0.0F;
  m_resolveBorder = true;
  applyPalette();
}

void Box::setRadius(float radius) {
  m_style.radius = radius;
  syncStyle();
}

void Box::setRadii(const Radii& radii) {
  m_style.radius = radii;
  syncStyle();
}

void Box::setCornerShapes(const CornerShapes& corners) {
  // Explicit shapes stay authoritative; the profile no longer overrides them.
  m_presentationChrome = false;
  m_presentationNormalCaptured = false;
  m_style.corners = corners;
  syncStyle();
}

void Box::setPresentationChrome(bool enabled) {
  if (m_presentationChrome == enabled) {
    return;
  }
  m_presentationChrome = enabled;
  if (enabled && !m_presentationNormalCaptured) {
    m_presentationNormal = m_style.corners;
    m_presentationNormalCaptured = true;
  }
  applyPalette();
}

void Box::setMaterial(RectMaterial material) {
  m_materialExplicit = true;
  m_material = material;
  applyPalette();
}

void Box::resetMaterial() {
  m_materialExplicit = false;
  m_material = RectMaterial::None;
  applyPalette();
}

void Box::enablePresentationChrome(const CornerShapes& normal) {
  m_presentationNormal = normal;
  m_presentationNormalCaptured = true;
  m_presentationChrome = true;
  applyPalette();
}

CornerShapes Box::presentationNormalShapes() const noexcept {
  return m_presentationNormalCaptured ? m_presentationNormal : m_style.corners;
}

void Box::setLogicalInset(const RectInsets& inset) {
  m_style.logicalInset = inset;
  syncStyle();
}

void Box::setSoftness(float softness) {
  m_style.softness = softness;
  syncStyle();
}

void Box::setNoAa(bool noAa) {
  m_style.noAa = noAa;
  syncStyle();
}

void Box::setSize(float w, float h) {
  Node::setSize(w, h);
  m_rect->setFrameSize(w, h);
}

void Box::setFrameSize(float w, float h) {
  Node::setFrameSize(w, h);
  m_rect->setFrameSize(w, h);
}

void Box::applyPalette() {
  if (m_resolveFill) {
    m_style.fill = resolveColorSpec(m_fill);
    m_style.fillMode = FillMode::Solid;
  }
  if (m_resolveBorder) {
    m_style.border = resolveColorSpec(m_border);
    m_style.borderWidth = m_borderWidth;
  }
  if (m_presentationNormalCaptured) {
    m_style.corners = m_presentationChrome ? Style::presentationCorners(m_presentationNormal) : m_presentationNormal;
  }
  m_style.material = m_materialExplicit
      ? m_material
      : (m_presentationChrome && Style::stagecraftEnabled() ? RectMaterial::Stagecraft : RectMaterial::None);
  syncStyle();
}

void Box::setFlatStyle() {
  m_fill = colorSpecFromRole(ColorRole::Surface);
  m_border = colorSpecFromRole(ColorRole::Outline);
  m_borderWidth = 0.0F;
  m_resolveFill = true;
  m_resolveBorder = true;
  m_style.fill = resolveColorSpec(m_fill);
  m_style.border = resolveColorSpec(m_border);
  m_style.borderWidth = m_borderWidth;
  m_style.fillMode = FillMode::Solid;
  m_style.corners = {};
  m_style.logicalInset = {};
  m_style.radius = 0;
  m_style.softness = 0;
  m_style.material = RectMaterial::None;
  m_presentationChrome = false;
  m_presentationNormalCaptured = false;
  m_materialExplicit = false;
  m_material = RectMaterial::None;
  syncStyle();
}

void Box::setPanelStyle(bool showBorder) {
  m_fill = colorSpecFromRole(ColorRole::Surface);
  if (showBorder) {
    m_border = colorSpecFromRole(ColorRole::Outline);
    m_borderWidth = Style::borderWidth;
  } else {
    m_border = clearColorSpec();
    m_borderWidth = 0.0F;
  }
  m_resolveFill = true;
  m_resolveBorder = true;
  m_style.fill = resolveColorSpec(m_fill);
  m_style.border = resolveColorSpec(m_border);
  m_style.borderWidth = m_borderWidth;
  m_style.fillMode = FillMode::Solid;
  m_style.corners = {};
  m_style.logicalInset = {};
  m_style.radius = Style::kumihimoEnabled() ? Style::scaledRadius(10.0F) : Style::scaledRadiusXl();
  m_style.softness = 1.0F;
  enablePresentationChrome({});
}

void Box::setDialogStyle() { setPanelStyle(true); }

void Box::setCardStyle(float scale, float fillOpacity, bool showBorder) {
  // Snapshot before the fill/border calls run applyPalette(), which would display
  // the active profile's chamfers and, if re-read afterwards, freeze them as normal.
  const CornerShapes normal = presentationNormalShapes();
  setFill(colorSpecFromRole(ColorRole::SurfaceVariant, fillOpacity));
  if (showBorder) {
    setBorder(colorSpecFromRole(ColorRole::Outline, Style::kumihimoEnabled() ? 0.5F : 1.0F), Style::borderWidth);
  } else {
    clearBorder();
  }
  setRadius(Style::kumihimoEnabled() ? Style::scaledRadiusMd(scale) : Style::scaledRadiusXl(scale));
  enablePresentationChrome(normal);
}

void Box::syncStyle() { m_rect->setStyle(m_style); }
