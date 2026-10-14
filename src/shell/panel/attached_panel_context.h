#pragma once

#include "render/core/render_styles.h"
#include "ui/style.h"

#include <cstdint>
#include <string_view>

enum class AttachedRevealDirection : std::uint8_t {
  Down,
  Up,
  Right,
  Left,
};

struct AttachedPanelGeometry {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  // Full panel corner radius; used for the away-side convex corners, which are visible
  // throughout the open/close animation and should always render rounded.
  float cornerRadius = 0.0F;
  // Bar-side concave-corner radius. Animated with the reveal progress: zero while the
  // bg's bar-side edge is still hidden behind the bar, ramps to cornerRadius as the
  // bulges slide into view near the end of the open animation.
  float bulgeRadius = 0.0F;
};

namespace attached_panel {

  [[nodiscard]] inline CornerShapes cornerShapes(std::string_view barPosition) {
    // Stagecraft replaces both the rounded away-side corners and the concave bar-side
    // notch with a uniform 45-degree cut, so the panel silhouette matches the chamfered
    // bar instead of butting a rounded/notched edge against it.
    if (Style::stagecraftEnabled()) {
      return Style::presentationCorners(CornerShapes{});
    }
    if (barPosition == "bottom") {
      return CornerShapes{
          .tl = CornerShape::Convex,
          .tr = CornerShape::Convex,
          .br = CornerShape::Concave,
          .bl = CornerShape::Concave,
      };
    }
    if (barPosition == "left") {
      return CornerShapes{
          .tl = CornerShape::Concave,
          .tr = CornerShape::Convex,
          .br = CornerShape::Convex,
          .bl = CornerShape::Concave,
      };
    }
    if (barPosition == "right") {
      return CornerShapes{
          .tl = CornerShape::Convex,
          .tr = CornerShape::Concave,
          .br = CornerShape::Concave,
          .bl = CornerShape::Convex,
      };
    }
    // top (default)
    return CornerShapes{
        .tl = CornerShape::Concave,
        .tr = CornerShape::Concave,
        .br = CornerShape::Convex,
        .bl = CornerShape::Convex,
    };
  }

  [[nodiscard]] inline RectInsets logicalInset(std::string_view barPosition, float radius) {
    // Stagecraft chamfers are inward cuts: the panel body is the full visual rect, so
    // there is no concave overhang and no outward inset.
    if (Style::stagecraftEnabled()) {
      return RectInsets{};
    }
    const bool vertical = (barPosition == "left" || barPosition == "right");
    if (vertical) {
      return RectInsets{
          .left = 0.0F,
          .top = radius,
          .right = 0.0F,
          .bottom = radius,
      };
    }
    return RectInsets{
        .left = radius,
        .top = 0.0F,
        .right = radius,
        .bottom = 0.0F,
    };
  }

  [[nodiscard]] inline AttachedRevealDirection revealDirection(std::string_view barPosition) {
    if (barPosition == "bottom") {
      return AttachedRevealDirection::Up;
    }
    if (barPosition == "left") {
      return AttachedRevealDirection::Right;
    }
    if (barPosition == "right") {
      return AttachedRevealDirection::Left;
    }
    return AttachedRevealDirection::Down;
  }

  [[nodiscard]] inline Radii cornerRadii(std::string_view barPosition, float radius) {
    if (barPosition == "bottom") {
      return Radii{0.0F, 0.0F, radius, radius};
    }
    if (barPosition == "left") {
      return Radii{0.0F, radius, radius, 0.0F};
    }
    if (barPosition == "right") {
      return Radii{radius, 0.0F, 0.0F, radius};
    }
    return Radii{radius, radius, 0.0F, 0.0F};
  }

} // namespace attached_panel
