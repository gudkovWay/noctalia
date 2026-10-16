#include "app/instance_context.h"
#include "core/deferred_call.h"
#include "dbus/bluetooth/bluetooth_agent.h"
#include "dbus/idle/screensaver_service.h"
#include "dbus/mpris/mpris_service.h"
#include "dbus/network/iwd_secret_agent.h"
#include "dbus/network/network_secret_agent.h"
#include "dbus/notification/notification_service.h"
#include "dbus/polkit/polkit_agent.h"
#include "dbus/session_bus.h"
#include "dbus/shared_service_policy.h"
#include "dbus/system_bus.h"
#include "dbus/tray/tray_service.h"
#include "debug/debug_service.h"
#include "notification/notification_manager.h"
#include "tests/test_check.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
  struct PrivateRoots {
    std::filesystem::path root;
    PrivateRoots() {
      char path[] = "/tmp/noctalia-multiwm-bus-XXXXXX";
      const char* created = ::mkdtemp(path);
      TEST_CHECK(created != nullptr);
      root = created;
      for (const char* variable :
           {"NOCTALIA_CONFIG_HOME", "NOCTALIA_STATE_HOME", "NOCTALIA_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME",
            "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
        ::setenv(variable, root.c_str(), 1);
      }
    }
    ~PrivateRoots() {
      std::error_code ec;
      std::filesystem::remove_all(root, ec);
    }
  };

  std::string owner(sdbus::IConnection& connection, const std::string& name) {
    auto proxy = sdbus::createProxy(
        connection, sdbus::ServiceName{"org.freedesktop.DBus"}, sdbus::ObjectPath{"/org/freedesktop/DBus"}
    );
    bool exists = false;
    proxy->callMethod("NameHasOwner").onInterface("org.freedesktop.DBus").withArguments(name).storeResultsTo(exists);
    if (!exists)
      return {};
    std::string result;
    proxy->callMethod("GetNameOwner").onInterface("org.freedesktop.DBus").withArguments(name).storeResultsTo(result);
    return result;
  }

  void refuses(const std::function<void()>& create) {
    bool refused = false;
    try {
      create();
    } catch (const sdbus::Error&) {
      refused = true;
    }
    TEST_CHECK(refused);
  }

  class Watcher {
  public:
    std::atomic<int> hosts = 0;
    std::unique_ptr<sdbus::IConnection> connection = sdbus::createSessionBusConnection();
    std::unique_ptr<sdbus::IObject> object =
        sdbus::createObject(*connection, sdbus::ObjectPath{"/StatusNotifierWatcher"});
    Watcher() {
      object
          ->addVTable(
              sdbus::registerMethod("RegisterStatusNotifierHost").implementedAs([this](const std::string& host) {
                TEST_CHECK(host.starts_with("org.kde.StatusNotifierHost-"));
                hosts.fetch_add(1);
              }),
              sdbus::registerProperty("RegisteredStatusNotifierItems").withGetter([]() {
                return std::vector<std::string>{};
              }),
              sdbus::registerProperty("IsStatusNotifierHostRegistered").withGetter([]() { return true; }),
              sdbus::registerProperty("ProtocolVersion").withGetter([]() { return std::int32_t{0}; }),
              sdbus::registerSignal("StatusNotifierItemRegistered").withParameters<std::string>(),
              sdbus::registerSignal("StatusNotifierItemUnregistered").withParameters<std::string>()
          )
          .forInterface("org.kde.StatusNotifierWatcher");
      connection->requestName(sdbus::ServiceName{"org.kde.StatusNotifierWatcher"});
      connection->enterEventLoopAsync();
    }
    ~Watcher() { connection->leaveEventLoop(); }
  };

  class Player {
  public:
    std::atomic<bool> paused = false;
    std::unique_ptr<sdbus::IConnection> connection = sdbus::createSessionBusConnection();
    std::unique_ptr<sdbus::IObject> object =
        sdbus::createObject(*connection, sdbus::ObjectPath{"/org/mpris/MediaPlayer2"});
    Player() {
      object
          ->addVTable(
              sdbus::registerProperty("Identity").withGetter([]() { return std::string{"Noctalia fixture"}; }),
              sdbus::registerProperty("DesktopEntry").withGetter([]() { return std::string{"noctalia-fixture"}; })
          )
          .forInterface("org.mpris.MediaPlayer2");
      object
          ->addVTable(
              sdbus::registerProperty("PlaybackStatus").withGetter([this]() {
                return std::string{paused.load() ? "Paused" : "Playing"};
              }),
              sdbus::registerProperty("Metadata").withGetter([]() {
                return std::map<std::string, sdbus::Variant>{
                    {"mpris:trackid", sdbus::Variant{sdbus::ObjectPath{"/fixture/track/one"}}},
                    {"xesam:title", sdbus::Variant{std::string{"Fixture track"}}}
                };
              }),
              sdbus::registerProperty("Position").withGetter([]() { return std::int64_t{0}; }),
              sdbus::registerProperty("Volume").withGetter([]() { return 1.0; }),
              sdbus::registerProperty("LoopStatus").withGetter([]() { return std::string{"None"}; }),
              sdbus::registerProperty("Shuffle").withGetter([]() { return false; }),
              sdbus::registerProperty("CanControl").withGetter([]() { return true; }),
              sdbus::registerProperty("CanPlay").withGetter([]() { return true; }),
              sdbus::registerProperty("CanPause").withGetter([]() { return true; }),
              sdbus::registerMethod("PlayPause").implementedAs([this]() {
                paused.store(true);
                object->emitSignal("PropertiesChanged")
                    .onInterface("org.freedesktop.DBus.Properties")
                    .withArguments(
                        std::string{"org.mpris.MediaPlayer2.Player"},
                        std::map<std::string, sdbus::Variant>{
                            {"PlaybackStatus", sdbus::Variant{std::string{"Paused"}}}
                        },
                        std::vector<std::string>{}
                    );
              })
          )
          .forInterface("org.mpris.MediaPlayer2.Player");
      connection->requestName(sdbus::ServiceName{"org.mpris.MediaPlayer2.noctalia_fixture"});
      connection->enterEventLoopAsync();
    }
    ~Player() { connection->leaveEventLoop(); }
  };
} // namespace

int main(int argc, char** argv) {
  TEST_CHECK(argc == 2);
  PrivateRoots roots;
  // Meson runs each mode on a private dbus-run-session bus, never the owner's bus.
  ::setenv("XDG_RUNTIME_DIR", "/tmp", 1);
  ::setenv("WAYLAND_DISPLAY", "noctalia-bus-fixture", 1);
  ::setenv("NOCTALIA_SESSION_ROLE", argv[1], 1);
  if (std::string(argv[1]) == "legacy")
    ::unsetenv("NOCTALIA_SESSION_ROLE");
  ::unsetenv("WAYLAND_SOCKET");
  noctalia::instance::exportSubprocessContext(
      noctalia::instance::current().statePath((roots.root / "noctalia").string())
  );
  auto observer = sdbus::createSessionBusConnection();
  SessionBus bus;
  if (std::string(argv[1]) == "primary") {
    noctalia::dbus::acquirePrimaryRole(bus.connection());
    TEST_CHECK(owner(*observer, "dev.noctalia.Primary") == bus.connection().getUniqueName());
    auto competitor = sdbus::createSessionBusConnection();
    bool refused = false;
    try {
      noctalia::dbus::acquirePrimaryRole(*competitor);
    } catch (const sdbus::Error&) {
      refused = true;
    }
    TEST_CHECK(refused);
    TEST_CHECK(owner(*observer, "dev.noctalia.Primary") == bus.connection().getUniqueName());

    // Even a replaceable external notification daemon must not be stolen.
    auto daemon = sdbus::createProxy(
        *competitor, sdbus::ServiceName{"org.freedesktop.DBus"}, sdbus::ObjectPath{"/org/freedesktop/DBus"}
    );
    std::uint32_t reply = 0;
    daemon->callMethod("RequestName")
        .onInterface("org.freedesktop.DBus")
        .withArguments(std::string("org.freedesktop.Notifications"), std::uint32_t{5})
        .storeResultsTo(reply);
    TEST_CHECK(reply == 1);
    refused = false;
    try {
      notification_dbus::acquireBusName(bus.connection());
    } catch (const sdbus::Error&) {
      refused = true;
    }
    TEST_CHECK(refused);
    TEST_CHECK(owner(*observer, "org.freedesktop.Notifications") == competitor->getUniqueName());
    competitor->releaseName(sdbus::ServiceName{"org.freedesktop.Notifications"});
    notification_dbus::acquireBusName(bus.connection());
    TEST_CHECK(notification_dbus::ownsBusName(bus.connection()));
    competitor->requestName(sdbus::ServiceName{"dev.noctalia.Debug"});
    NotificationManager notifications;
    refuses([&]() { DebugService debug(bus, notifications); });
    TEST_CHECK(owner(*observer, "dev.noctalia.Debug") == competitor->getUniqueName());
    competitor->requestName(sdbus::ServiceName{"dev.noctalia.Mpris"});
    MprisService media(bus);
    TEST_CHECK(!media.hasControlApi());
    TEST_CHECK(media.players().empty());
    TEST_CHECK(owner(*observer, "dev.noctalia.Mpris") == competitor->getUniqueName());
    competitor->requestName(sdbus::ServiceName{"org.freedesktop.ScreenSaver"});
    ScreenSaverService idle(nullptr);
    TEST_CHECK(!idle.hasScreenSaverBus());
    TEST_CHECK(owner(*observer, "org.freedesktop.ScreenSaver") == competitor->getUniqueName());
  } else if (std::string(argv[1]) == "legacy") {
    TEST_CHECK(noctalia::instance::current().ownsGlobalServices());
    noctalia::dbus::acquirePrimaryRole(bus.connection());
    TEST_CHECK(owner(*observer, "dev.noctalia.Primary").empty());
    notification_dbus::acquireBusName(bus.connection());
    NotificationManager notifications;
    DebugService debug(bus, notifications);
    MprisService media(bus);
    ScreenSaverService idle(nullptr);
    TEST_CHECK(media.hasControlApi());
    TEST_CHECK(idle.hasScreenSaverBus());
    for (const std::string name : {"org.freedesktop.Notifications", "dev.noctalia.Debug", "dev.noctalia.Mpris"}) {
      TEST_CHECK(owner(*observer, name) == bus.connection().getUniqueName());
    }
    TEST_CHECK(!owner(*observer, "org.freedesktop.ScreenSaver").empty());
  } else {
    TEST_CHECK(!noctalia::instance::current().ownsGlobalServices());
    noctalia::dbus::acquirePrimaryRole(bus.connection());
    TEST_CHECK(owner(*observer, "dev.noctalia.Primary").empty());
    bool refused = false;
    try {
      notification_dbus::acquireBusName(bus.connection());
    } catch (const sdbus::Error&) {
      refused = true;
    }
    TEST_CHECK(refused);
    NotificationManager notifications;
    refused = false;
    try {
      DebugService debug(bus, notifications);
    } catch (const sdbus::Error&) {
      refused = true;
    }
    TEST_CHECK(refused);
    MprisService media(bus); // ordinary discovery survives without global control export
    TEST_CHECK(!media.hasControlApi());
    {
      int changes = 0;
      media.setChangeCallback([&]() { ++changes; });
      Player player;
      media.refreshPlayers();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (media.players().empty() && std::chrono::steady_clock::now() < deadline) {
        bus.processPendingEvents();
        for (auto& call : DeferredCall::takePending())
          call();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      TEST_CHECK(media.players().contains("org.mpris.MediaPlayer2.noctalia_fixture"));
      TEST_CHECK(media.players().at("org.mpris.MediaPlayer2.noctalia_fixture").title == "Fixture track");
      TEST_CHECK(media.playPause("org.mpris.MediaPlayer2.noctalia_fixture"));
      while (!player.paused.load() && std::chrono::steady_clock::now() < deadline) {
        bus.processPendingEvents();
        for (auto& call : DeferredCall::takePending())
          call();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      TEST_CHECK(player.paused.load());
      TEST_CHECK(changes > 0);
      TEST_CHECK(!media.hasControlApi());
      TEST_CHECK(owner(*observer, "dev.noctalia.Mpris").empty());
      media.setChangeCallback({});
    }
    ScreenSaverService idle(nullptr);
    TEST_CHECK(!idle.hasScreenSaverBus());
    for (const std::string name :
         {"dev.noctalia.Primary", "org.freedesktop.Notifications", "dev.noctalia.Debug", "dev.noctalia.Mpris",
          "org.freedesktop.ScreenSaver"}) {
      TEST_CHECK(owner(*observer, name).empty());
    }
    auto external = sdbus::createSessionBusConnection();
    for (const std::string name :
         {"org.freedesktop.Notifications", "dev.noctalia.Debug", "dev.noctalia.Mpris", "org.freedesktop.ScreenSaver"}) {
      external->requestName(sdbus::ServiceName{name});
    }
    refuses([&]() { NotificationService daemon(bus, notifications); });
    refuses([&]() { DebugService debug(bus, notifications); });
    for (const std::string name :
         {"org.freedesktop.Notifications", "dev.noctalia.Debug", "dev.noctalia.Mpris", "org.freedesktop.ScreenSaver"}) {
      TEST_CHECK(owner(*observer, name) == external->getUniqueName());
    }
    const char* privateBus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    TEST_CHECK(privateBus != nullptr);
    ::setenv("DBUS_SYSTEM_BUS_ADDRESS", privateBus, 1);
    SystemBus system;
    refuses([&]() { NetworkSecretAgent agent(system); });
    refuses([&]() { IwdSecretAgent agent(system); });
    refuses([&]() { BluetoothAgent agent(system); });
    refuses([&]() { PolkitAgent agent(system); });
    TrayService tray(bus);
    tray.start();
    TEST_CHECK(owner(*observer, "org.kde.StatusNotifierWatcher").empty());
    {
      Watcher watcher;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (watcher.hosts.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        bus.processPendingEvents();
        for (auto& call : DeferredCall::takePending())
          call();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      TEST_CHECK(watcher.hosts.load() > 0);
      TEST_CHECK(owner(*observer, "org.kde.StatusNotifierWatcher") == watcher.connection->getUniqueName());
    }
    bus.processPendingEvents();
    TEST_CHECK(owner(*observer, "org.kde.StatusNotifierWatcher").empty());
  }
}
