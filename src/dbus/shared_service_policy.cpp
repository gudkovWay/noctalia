#include "dbus/shared_service_policy.h"

#include <cstdint>
#include <string>

namespace noctalia::dbus {
  namespace {
    void acquireUnqueued(sdbus::IConnection& connection, const sdbus::ServiceName& name) {
      auto daemon = sdbus::createProxy(
          connection, sdbus::ServiceName{"org.freedesktop.DBus"}, sdbus::ObjectPath{"/org/freedesktop/DBus"}
      );
      std::uint32_t reply = 0;
      daemon->callMethod("RequestName")
          .onInterface("org.freedesktop.DBus")
          .withArguments(std::string(name), std::uint32_t{4})
          .storeResultsTo(reply);
      if (reply != 1 && reply != 4) {
        throw sdbus::Error(
            sdbus::Error::Name{"org.freedesktop.DBus.Error.AccessDenied"},
            "shared service already owned: " + std::string(name)
        );
      }
    }
  } // namespace

  void requireGlobalRole(std::string_view service) {
    if (!instance::current().ownsGlobalServices()) {
      throw sdbus::Error(
          sdbus::Error::Name{"org.freedesktop.DBus.Error.AccessDenied"},
          "secondary instance does not export/register " + std::string(service)
      );
    }
  }

  void acquirePrimaryRole(sdbus::IConnection& connection) {
    if (instance::current().role == instance::Role::Primary) {
      acquireUnqueued(connection, sdbus::ServiceName{"dev.noctalia.Primary"});
    }
  }

  void acquireServiceName(sdbus::IConnection& connection, const sdbus::ServiceName& name) {
    requireGlobalRole(std::string(name));
    if (instance::current().isolated())
      acquireUnqueued(connection, name);
    else
      connection.requestName(name);
  }
} // namespace noctalia::dbus
