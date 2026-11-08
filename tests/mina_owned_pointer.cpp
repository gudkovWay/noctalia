#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

// Test-only wlroots virtual-pointer v2 requests, from wlr-protocols (MIT,
// Copyright 2019 Josef Gajdusek). Never bind to an unverified live compositor.
namespace {
  extern const wl_interface pointerInterface;
  const wl_interface* managerTypes[] = {&wl_seat_interface, &wl_output_interface, &pointerInterface};
  const wl_message pointerRequests[] = {{"motion", "uff", nullptr},   {"motion_absolute", "uuuuu", nullptr},
                                        {"button", "uuu", nullptr},   {"axis", "uuf", nullptr},
                                        {"frame", "", nullptr},       {"axis_source", "u", nullptr},
                                        {"axis_stop", "uu", nullptr}, {"axis_discrete", "uufi", nullptr},
                                        {"destroy", "", nullptr}};
  const wl_interface pointerInterface = {"zwlr_virtual_pointer_v1", 2, 9, pointerRequests, 0, nullptr};
  const wl_message managerRequests[] = {
      {"create_virtual_pointer", "?on", managerTypes},
      {"destroy", "", nullptr},
      {"create_virtual_pointer_with_output", "2?o?on", managerTypes}
  };
  const wl_interface managerInterface = {"zwlr_virtual_pointer_manager_v1", 2, 3, managerRequests, 0, nullptr};
  struct Output {
    wl_output* proxy{};
    std::string name;
  };
  struct Registry {
    wl_proxy* manager{};
    wl_seat* seat{};
    std::vector<Output*> outputs;
  };
  void geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
  void mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
  void done(void*, wl_output*) {}
  void scale(void*, wl_output*, int32_t) {}
  void name(void* data, wl_output*, const char* value) { static_cast<Output*>(data)->name = value; }
  void description(void*, wl_output*, const char*) {}
  const wl_output_listener outputListener{geometry, mode, done, scale, name, description};
  void global(void* data, wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
    auto& r = *static_cast<Registry*>(data);
    if (std::strcmp(interface, managerInterface.name) == 0 && version >= 2)
      r.manager = static_cast<wl_proxy*>(wl_registry_bind(registry, id, &managerInterface, 2));
    else if (std::strcmp(interface, "wl_seat") == 0 && !r.seat)
      r.seat = static_cast<wl_seat*>(wl_registry_bind(registry, id, &wl_seat_interface, 1));
    else if (std::strcmp(interface, "wl_output") == 0 && version >= 4) {
      auto* output = new Output{static_cast<wl_output*>(wl_registry_bind(registry, id, &wl_output_interface, 4))};
      r.outputs.push_back(output);
      wl_output_add_listener(output->proxy, &outputListener, output);
    }
  }
  void removed(void*, wl_registry*, uint32_t) {}
} // namespace

int main(int argc, char** argv) {
  const bool serve = argc == 3 && std::strcmp(argv[1], "--serve") == 0;
  if ((!serve && argc != 5) || !std::getenv("VD_ROOT") || !std::getenv("HYPRLAND_INSTANCE_SIGNATURE"))
    return 2;
  const int expected = std::atoi(argv[serve ? 2 : 1]);
  if (expected <= 1 || expected == 6629)
    return 2;
  std::ifstream command("/proc/" + std::to_string(expected) + "/cmdline");
  const std::string bytes((std::istreambuf_iterator<char>(command)), {});
  if (bytes.find(".superpowers/runtime/hyprland.lua") == std::string::npos)
    return 2;
  auto* display = wl_display_connect(nullptr);
  if (!display)
    return 3;
  ucred peer{};
  socklen_t length = sizeof(peer);
  if (getsockopt(wl_display_get_fd(display), SOL_SOCKET, SO_PEERCRED, &peer, &length)
      || peer.pid != expected
      || peer.uid != getuid())
    return 4;
  Registry state;
  auto* registry = wl_display_get_registry(display);
  const wl_registry_listener listener{global, removed};
  wl_registry_add_listener(registry, &listener, &state);
  if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0)
    return 5;
  auto output = std::ranges::find_if(state.outputs, [](const auto* o) { return o->name == "HEADLESS-1"; });
  if (!state.manager || output == state.outputs.end())
    return 6;
  auto* pointer = static_cast<wl_proxy*>(
      wl_proxy_marshal_flags(state.manager, 2, &pointerInterface, 2, 0, state.seat, (*output)->proxy, nullptr)
  );
  const auto dispatch = [&](int inputX, int inputY, unsigned button) {
    const auto now = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
    );
    const unsigned x = std::clamp(inputX, 0, 3439), y = std::clamp(inputY, 0, 1439);
    wl_proxy_marshal_flags(pointer, 1, nullptr, 2, 0, now, x, y, 3440U, 1440U);
    wl_proxy_marshal_flags(pointer, 4, nullptr, 2, 0);
    wl_display_roundtrip(display);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (button) {
      wl_proxy_marshal_flags(pointer, 2, nullptr, 2, 0, now + 80, button, 1U);
      wl_proxy_marshal_flags(pointer, 4, nullptr, 2, 0);
      wl_display_roundtrip(display);
      std::this_thread::sleep_for(std::chrono::milliseconds(60));
      wl_proxy_marshal_flags(pointer, 2, nullptr, 2, 0, now + 140, button, 0U);
      wl_proxy_marshal_flags(pointer, 4, nullptr, 2, 0);
      wl_display_roundtrip(display);
    }
  };
  int result = 0;
  if (serve) {
    std::array<char, 128> line{};
    while (std::fgets(line.data(), line.size(), stdin)) {
      int x = 0, y = 0;
      unsigned button = 0;
      char extra = '\0';
      if (std::sscanf(line.data(), "%d %d %u %c", &x, &y, &button, &extra) != 3) {
        result = 7;
        break;
      }
      dispatch(x, y, button);
      std::puts("ok");
      std::fflush(stdout);
    }
  } else {
    dispatch(std::atoi(argv[2]), std::atoi(argv[3]), static_cast<unsigned>(std::atoi(argv[4])));
  }
  wl_proxy_marshal_flags(pointer, 8, nullptr, 2, WL_MARSHAL_FLAG_DESTROY);
  wl_proxy_marshal_flags(state.manager, 1, nullptr, 2, WL_MARSHAL_FLAG_DESTROY);
  for (auto* o : state.outputs) {
    wl_output_destroy(o->proxy);
    delete o;
  }
  if (state.seat)
    wl_seat_destroy(state.seat);
  wl_registry_destroy(registry);
  wl_display_disconnect(display);
  return result;
}
