#pragma once

#include "shell/tray/tray_fan_geometry.h"

#include <format>
#include <string>
#include <string_view>

namespace tray {
  // Material only: tray logos and actions remain native scene nodes.
  inline constexpr std::string_view kFanButtonSvg =
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="40" height="32" viewBox="0 0 40 32">
<defs><linearGradient id="paper" x2="0" y2="1"><stop stop-color="#d7c8a4"/><stop offset="1" stop-color="#82775e"/></linearGradient></defs>
<path d="M20 27L3 11Q20 -3 37 11Z" fill="url(#paper)" stroke="#e4cf9b" stroke-width="1.4"/>
<path d="M20 27L3 11M20 27L8 7M20 27L14 4M20 27L20 3M20 27L26 4M20 27L32 7M20 27L37 11" stroke="#51402c" stroke-width="1.2"/>
<path d="M20 27v4" stroke="#972b36" stroke-width="2"/><circle cx="20" cy="27" r="2.1" fill="#d8b76e" stroke="#543822"/>
</svg>)svg";

  // Neutral luminance textures; the scene supplies live semantic palette tints.
  [[nodiscard]] inline std::string fanPaperSvg(std::size_t leaves) {
    const float angle = kFanSweep / static_cast<float>(std::max<std::size_t>(1, leaves));
    const float edgeX = 400.0F * (1.0F - std::cos(angle));
    const float edgeY = 400.0F * std::sin(angle);
    return std::format(
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400" viewBox="0 0 400 400">
<defs>
<linearGradient id="paper" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#ffffff"/><stop offset=".4" stop-color="#f4f4f4"/><stop offset="1" stop-color="#e8e8e8"/></linearGradient>
<pattern id="fiber" width="37" height="11" patternUnits="userSpaceOnUse"><path d="M0 3l26 1m-9 5l20 -1" stroke="#000000" stroke-opacity=".03" stroke-width=".7"/></pattern>
</defs>
<path d="M400 0L0 0A400 400 0 0 0 {0:.3f} {1:.3f}Z" fill="url(#paper)"/>
<path d="M400 0L0 0A400 400 0 0 0 {0:.3f} {1:.3f}Z" fill="url(#fiber)"/>
</svg>)svg",
        edgeX, edgeY
    );
  }

  // Spines only. The continuous outer edge belongs to the whole fan, not a leaf.
  inline constexpr std::string_view kFanRibsSvg =
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400" viewBox="0 0 400 400">
<defs><linearGradient id="wood" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#ffffff"/><stop offset=".5" stop-color="#777777"/><stop offset="1" stop-color="#dddddd"/></linearGradient></defs>
<path d="M400 0L3 0" stroke="#666666" stroke-width="5"/>
<path d="M400 0L3 0" stroke="url(#wood)" stroke-width="3"/>
<path d="M388 1L23 1" stroke="#ffffff" stroke-opacity=".3" stroke-width=".8"/>
<path d="M318 0h-9M133 0h-9" stroke="#ffffff" stroke-width="2.5"/>
</svg>)svg";

  // Eight-pixel padding preserves anti-aliasing and round caps outside the paper.
  inline constexpr std::string_view kFanOutlineSvg =
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="416" height="416" viewBox="-8 -8 416 416">
<path d="M0 0A400 400 0 0 0 400 400" fill="none" stroke="#ffffff" stroke-opacity=".22" stroke-width="5" stroke-linecap="round"/>
<path d="M0 0A400 400 0 0 0 400 400" fill="none" stroke="#ffffff" stroke-opacity=".75" stroke-width="2" stroke-linecap="round"/>
</svg>)svg";

  inline constexpr std::string_view kFanPivotSvg =
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="100" viewBox="0 0 32 100">
<defs><radialGradient id="brass"><stop stop-color="#e3c48a"/><stop offset=".6" stop-color="#b99150"/><stop offset="1" stop-color="#58432b"/></radialGradient></defs>
<path d="M16 22v39q-6 2-4 9h8q2-7-4-9M11 73l-3 20M14 73l-1 24M17 73v24M20 73l4 20" fill="none" stroke="#7e2733" stroke-width="2.5"/>
<circle cx="16" cy="16" r="13" fill="#402621" stroke="#b18a52" stroke-width="2"/><circle cx="16" cy="16" r="8" fill="url(#brass)"/>
<circle cx="16" cy="16" r="2" fill="#735332"/>
</svg>)svg";
} // namespace tray
