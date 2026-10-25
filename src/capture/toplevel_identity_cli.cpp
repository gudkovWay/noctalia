#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "hyprland-toplevel-mapping-v1-client-protocol.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

namespace {
  struct Window {
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    hyprland_toplevel_window_mapping_handle_v1* mapping = nullptr;
    std::string identifier;
    std::uint64_t address = 0;
    bool closed = false;
  };
  struct State {
    ext_foreign_toplevel_list_v1* list = nullptr;
    hyprland_toplevel_mapping_manager_v1* mappings = nullptr;
    std::vector<std::unique_ptr<Window>> windows;
  };
  const ext_foreign_toplevel_handle_v1_listener windowListener = {
      .closed = [](void* data, auto*) { static_cast<Window*>(data)->closed = true; },
      .done = [](void*, auto*) {},
      .title = [](void*, auto*, const char*) {},
      .app_id = [](void*, auto*, const char*) {},
      .identifier = [](void* data, auto*, const char* id) { static_cast<Window*>(data)->identifier = id; },
  };
  const hyprland_toplevel_window_mapping_handle_v1_listener mappingListener = {
      .window_address = [](void* data, auto*, std::uint32_t hi,
                           std::uint32_t lo) { static_cast<Window*>(data)->address = (std::uint64_t{hi} << 32) | lo; },
      .failed = [](void*, auto*) {},
  };
  const ext_foreign_toplevel_list_v1_listener listListener = {
      .toplevel =
          [](void* data, auto*, ext_foreign_toplevel_handle_v1* handle) {
            auto& state = *static_cast<State*>(data);
            auto window = std::make_unique<Window>();
            window->handle = handle;
            ext_foreign_toplevel_handle_v1_add_listener(handle, &windowListener, window.get());
            state.windows.push_back(std::move(window));
          },
      .finished = [](void*, auto*) {},
  };
  const wl_registry_listener registryListener = {
      .global =
          [](void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t) {
            auto& state = *static_cast<State*>(data);
            if (std::strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
              state.list = static_cast<ext_foreign_toplevel_list_v1*>(
                  wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1)
              );
              ext_foreign_toplevel_list_v1_add_listener(state.list, &listListener, &state);
            } else if (std::strcmp(interface, hyprland_toplevel_mapping_manager_v1_interface.name) == 0) {
              state.mappings = static_cast<hyprland_toplevel_mapping_manager_v1*>(
                  wl_registry_bind(registry, name, &hyprland_toplevel_mapping_manager_v1_interface, 1)
              );
            }
          },
      .global_remove = [](void*, wl_registry*, std::uint32_t) {},
  };
} // namespace

int main(int argc, char** argv) {
  std::set<std::uint64_t> requested;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.size() < 3
        || arg.size() > 18
        || !arg.starts_with("0x")
        || arg.find_first_not_of("0123456789abcdefABCDEF", 2) != std::string::npos) {
      std::fputs("expected exact 0x window addresses\n", stderr);
      return 2;
    }
    requested.insert(std::stoull(arg, nullptr, 16));
  }
  if (requested.empty()) {
    std::fputs("usage: agent-workspace-toplevels 0xADDRESS [...]\n", stderr);
    return 2;
  }
  // Bound protocol roundtrips even when the compositor stops responding.
  alarm(2);
  wl_display* display = wl_display_connect(nullptr);
  if (!display) {
    std::fputs("cannot connect to the selected Wayland display\n", stderr);
    return 3;
  }
  State state;
  wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registryListener, &state);
  if (wl_display_roundtrip(display) < 0 || !state.list || !state.mappings || wl_display_roundtrip(display) < 0) {
    std::fputs("ext-foreign-toplevel-list / Hyprland mapping unavailable\n", stderr);
    wl_display_disconnect(display);
    return 3;
  }
  for (auto& window : state.windows) {
    if (!window->closed) {
      window->mapping = hyprland_toplevel_mapping_manager_v1_get_window_for_toplevel(state.mappings, window->handle);
      hyprland_toplevel_window_mapping_handle_v1_add_listener(window->mapping, &mappingListener, window.get());
    }
  }
  if (wl_display_roundtrip(display) < 0) {
    wl_display_disconnect(display);
    return 3;
  }
  nlohmann::json result = nlohmann::json::object();
  for (const auto& window : state.windows) {
    if (!window->closed && requested.contains(window->address) && !window->identifier.empty()) {
      char address[19];
      std::snprintf(address, sizeof address, "0x%llx", static_cast<unsigned long long>(window->address));
      result[address] = window->identifier;
    }
  }
  std::puts(result.dump().c_str());
  for (const auto& window : state.windows) {
    if (window->mapping)
      hyprland_toplevel_window_mapping_handle_v1_destroy(window->mapping);
    ext_foreign_toplevel_handle_v1_destroy(window->handle);
  }
  ext_foreign_toplevel_list_v1_destroy(state.list);
  hyprland_toplevel_mapping_manager_v1_destroy(state.mappings);
  wl_registry_destroy(registry);
  wl_display_disconnect(display);
  return 0;
}
