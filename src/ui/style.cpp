#include "ui/style.h"

#include "ui/palette.h"

#include <algorithm>

namespace {

  float g_cornerRadiusScale = 1.0F;
  bool g_buttonBordersEnabled = true;
  bool g_inputBordersEnabled = true;
  bool g_popupBordersEnabled = true;
  bool g_rtl = false;
  bool g_popupShadowsEnabled = true;
  bool g_cardBordersEnabled = true;
  bool g_stagecraftEnabled = false;
  bool g_kumihimoEnabled = false;

} // namespace

namespace Style {

  float cornerRadiusScale() noexcept { return g_cornerRadiusScale; }

  void setCornerRadiusScale(float scale) noexcept { g_cornerRadiusScale = std::clamp(scale, 0.0F, 2.0F); }

  bool buttonBordersEnabled() noexcept { return g_stagecraftEnabled || g_kumihimoEnabled || g_buttonBordersEnabled; }

  void setButtonBordersEnabled(bool enabled) {
    if (g_buttonBordersEnabled == enabled) {
      return;
    }
    g_buttonBordersEnabled = enabled;
    buttonBordersChanged().emit();
  }

  Signal<>& buttonBordersChanged() {
    static Signal<> signal;
    return signal;
  }

  bool inputBordersEnabled() noexcept { return g_stagecraftEnabled || g_kumihimoEnabled || g_inputBordersEnabled; }

  void setInputBordersEnabled(bool enabled) {
    if (g_inputBordersEnabled == enabled) {
      return;
    }
    g_inputBordersEnabled = enabled;
    inputBordersChanged().emit();
  }

  Signal<>& inputBordersChanged() {
    static Signal<> signal;
    return signal;
  }

  bool popupBordersEnabled() noexcept { return g_popupBordersEnabled; }
  void setPopupBordersEnabled(bool enabled) { g_popupBordersEnabled = enabled; }

  bool rtl() noexcept { return g_rtl; }
  void setRtl(bool rtl) noexcept { g_rtl = rtl; }
  bool cardBordersEnabled() noexcept { return g_stagecraftEnabled || g_kumihimoEnabled || g_cardBordersEnabled; }
  void setCardBordersEnabled(bool enabled) { g_cardBordersEnabled = enabled; }

  bool popupShadowsEnabled() noexcept { return g_popupShadowsEnabled; }
  void setPopupShadowsEnabled(bool enabled) { g_popupShadowsEnabled = enabled; }

  bool stagecraftEnabled() noexcept { return g_stagecraftEnabled; }

  void setStagecraftEnabled(bool enabled) {
    if (g_stagecraftEnabled == enabled) {
      return;
    }
    g_stagecraftEnabled = enabled;
    // Reuse the palette invalidation channel: geometry subscribers rebuild their scene
    // nodes on paletteChanged even when the palette colors themselves are unchanged.
    paletteChanged().emit();
  }

  bool kumihimoEnabled() noexcept { return g_kumihimoEnabled; }

  void setKumihimoEnabled(bool enabled) {
    if (g_kumihimoEnabled == enabled) {
      return;
    }
    g_kumihimoEnabled = enabled;
    paletteChanged().emit();
  }

  RectMaterial panelMaterial() noexcept {
    return g_kumihimoEnabled ? RectMaterial::Kumihimo
                             : (g_stagecraftEnabled ? RectMaterial::Stagecraft : RectMaterial::None);
  }

  CornerShapes presentationCorners(CornerShapes normal) noexcept {
    if (!g_stagecraftEnabled) {
      return normal;
    }
    return CornerShapes{
        .tl = CornerShape::Chamfer,
        .tr = CornerShape::Chamfer,
        .br = CornerShape::Chamfer,
        .bl = CornerShape::Chamfer,
    };
  }

  float scaledRadius(float radius, float localScale) noexcept { return radius * localScale * g_cornerRadiusScale; }

  float scaledRadiusSm(float localScale) noexcept { return scaledRadius(radiusSm, localScale); }

  float scaledRadiusMd(float localScale) noexcept { return scaledRadius(radiusMd, localScale); }

  float scaledRadiusLg(float localScale) noexcept { return scaledRadius(radiusLg, localScale); }

  float scaledRadiusXl(float localScale) noexcept { return scaledRadius(radiusXl, localScale); }

} // namespace Style
