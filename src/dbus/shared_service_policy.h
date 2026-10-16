#pragma once

#include "app/instance_context.h"

#include <sdbus-c++/sdbus-c++.h>
#include <string_view>

namespace noctalia::dbus {
  // Secondary guards run before exporting an object, not merely before RequestName.
  void requireGlobalRole(std::string_view service);
  // Primary-only process-lifetime lease on the shared user bus. No replacement,
  // queueing, promotion or auto-election. Legacy and secondary do not claim it.
  void acquirePrimaryRole(sdbus::IConnection& connection);
  // Legacy keeps sdbus's historical behavior. Multi-session never replaces or
  // queues behind an existing owner; callers can degrade only this service.
  void acquireServiceName(sdbus::IConnection& connection, const sdbus::ServiceName& name);
} // namespace noctalia::dbus
