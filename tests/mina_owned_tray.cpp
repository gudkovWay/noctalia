#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
  const char* bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
  const char* system = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
  const char* home = std::getenv("HOME");
  if ((argc != 2 && argc != 3)
      || !bus
      || !system
      || std::string(bus) != system
      || !home
      || std::string(home).find(".superpowers/runtime/home") == std::string::npos
      || !std::getenv("VD_ROOT"))
    return 2;
  const int count = argc == 3 ? std::atoi(argv[2]) : 3;
  if (count < 0 || count > 17)
    return 2;
  std::ofstream events(argv[1], std::ios::app);
  std::mutex eventMutex;
  using Layout = sdbus::Struct<int32_t, std::map<std::string, sdbus::Variant>, std::vector<sdbus::Variant>>;
  std::vector<std::unique_ptr<sdbus::IConnection>> connections;
  std::vector<std::unique_ptr<sdbus::IObject>> objects;
  const std::array<std::string, 3> icons{"utilities-terminal", "audio-x-generic", "network-wired"};
  for (int i = 0; i < count; ++i) {
    auto connection = sdbus::createSessionBusConnection();
    const std::string id = "Mina native fixture " + std::to_string(i + 1);
    const std::string name = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-" + std::to_string(i + 1);
    connection->requestName(sdbus::ServiceName{name});
    const std::string itemPath = "/StatusNotifierItem" + std::to_string(i + 1);
    auto item = sdbus::createObject(*connection, sdbus::ObjectPath{itemPath});
    item->addVTable(
            sdbus::registerProperty("Id").withGetter([id] { return id; }),
            sdbus::registerProperty("Title").withGetter([id] { return id; }),
            sdbus::registerProperty("Status").withGetter([] { return std::string{"Active"}; }),
            sdbus::registerProperty("Category").withGetter([] { return std::string{"ApplicationStatus"}; }),
            sdbus::registerProperty("IconName").withGetter([icon = icons[static_cast<std::size_t>(i) % icons.size()]] { return icon; }),
            sdbus::registerProperty("ItemIsMenu").withGetter([] { return false; }),
            sdbus::registerProperty("Menu").withGetter([] { return sdbus::ObjectPath{"/Menu"}; }),
            sdbus::registerMethod("Activate").implementedAs([&events, &eventMutex, i](int32_t, int32_t) {
              const std::lock_guard lock(eventMutex);
              events << i << " Activate\n" << std::flush;
            }),
            sdbus::registerMethod("ContextMenu").implementedAs([&events, &eventMutex, i](int32_t, int32_t) {
              const std::lock_guard lock(eventMutex);
              events << i << " ContextMenu\n" << std::flush;
            })
    ).forInterface("org.kde.StatusNotifierItem");
    auto menu = sdbus::createObject(*connection, sdbus::ObjectPath{"/Menu"});
    menu->addVTable(
            sdbus::registerProperty("Version").withGetter([] { return uint32_t{4}; }),
            sdbus::registerMethod("AboutToShow").implementedAs([](int32_t) { return false; }),
            sdbus::registerMethod("GetLayout").implementedAs([id](int32_t, int32_t, const std::vector<std::string>&) {
              Layout child{
                  1,
                  {{"label", sdbus::Variant{"Activate " + id}},
                   {"enabled", sdbus::Variant{true}},
                   {"visible", sdbus::Variant{true}}},
                  {}
              };
              return std::tuple<uint32_t, Layout>{1, Layout{0, {}, {sdbus::Variant{child}}}};
            }),
            sdbus::registerMethod("Event").implementedAs(
                [&events, &eventMutex, i](int32_t entry, const std::string& event, const sdbus::Variant&, uint32_t) {
                  const std::lock_guard lock(eventMutex);
                  events << i << " Menu " << entry << ' ' << event << '\n' << std::flush;
                }
            )
    ).forInterface("com.canonical.dbusmenu");
    connection->enterEventLoopAsync();
    auto watcher = sdbus::createProxy(
        *connection, sdbus::ServiceName{"org.kde.StatusNotifierWatcher"}, sdbus::ObjectPath{"/StatusNotifierWatcher"}
    );
    watcher->callMethod("RegisterStatusNotifierItem")
        .onInterface("org.kde.StatusNotifierWatcher")
        .withArguments(name + itemPath);
    objects.push_back(std::move(item));
    objects.push_back(std::move(menu));
    connections.push_back(std::move(connection));
  }
  {
    const std::lock_guard lock(eventMutex);
    events << "registered " << count << " real SNI fixtures\n" << std::flush;
  }
  std::this_thread::sleep_for(std::chrono::seconds(90));
  for (auto& connection : connections)
    connection->leaveEventLoop();
}
