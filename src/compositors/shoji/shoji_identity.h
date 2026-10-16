#pragma once

#include <string>
#include <string_view>

namespace compositors::shoji {
  // Linux abstract AF_UNIX address, including its leading NUL. Matches the
  // installed compositor/helper contract: FNV-1a of canonical Wayland path.
  [[nodiscard]] std::string nativeAddress(std::string_view canonicalWaylandSocket);
  // Read-only bounded identity transaction. No temperature/power/wake/exit
  // commands: those capabilities are NOT implied by a successful identity.
  [[nodiscard]] bool hasNativeIdentity(std::string_view canonicalWaylandSocket);
  [[nodiscard]] bool selectedInstanceIsShoji();
} // namespace compositors::shoji
