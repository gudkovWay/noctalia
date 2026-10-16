#include "compositors/compositor_detect.h"
#include "compositors/compositor_runtime.h"
#include "ext-workspace-v1-test-server-protocol.h"
#include "tests/test_check.h"
#include "wayland/wayland_workspaces.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>

namespace {
  struct WorkspaceServer {
    wl_display* display = wl_display_create();
    std::atomic<int> activations = 0;
    std::atomic<int> commits = 0;
    std::jthread worker;

    static void destroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
    static void activate(wl_client*, wl_resource* resource) {
      static_cast<WorkspaceServer*>(wl_resource_get_user_data(resource))->activations.fetch_add(1);
    }
    static void ignore(wl_client*, wl_resource*) {}
    static void assign(wl_client*, wl_resource*, wl_resource*) {}
    static void commit(wl_client*, wl_resource* resource) {
      static_cast<WorkspaceServer*>(wl_resource_get_user_data(resource))->commits.fetch_add(1);
    }
    static void stop(wl_client*, wl_resource* resource) {
      ext_workspace_manager_v1_send_finished(resource);
      wl_resource_destroy(resource);
    }
    static void bind(wl_client* client, void* data, std::uint32_t, std::uint32_t id) {
      auto* manager = wl_resource_create(client, &ext_workspace_manager_v1_interface, 1, id);
      TEST_CHECK(manager != nullptr);
      static const struct ext_workspace_manager_v1_interface managerImpl{.commit = commit, .stop = stop};
      wl_resource_set_implementation(manager, &managerImpl, data, nullptr);
      auto* workspace = wl_resource_create(client, &ext_workspace_handle_v1_interface, 1, 0);
      TEST_CHECK(workspace != nullptr);
      static const struct ext_workspace_handle_v1_interface workspaceImpl{
          .destroy = destroy, .activate = activate, .deactivate = ignore, .assign = assign, .remove = ignore
      };
      wl_resource_set_implementation(workspace, &workspaceImpl, data, nullptr);
      ext_workspace_manager_v1_send_workspace(manager, workspace);
      ext_workspace_handle_v1_send_id(workspace, "DP-2:11");
      ext_workspace_handle_v1_send_name(workspace, "甲");
      wl_array coordinates;
      wl_array_init(&coordinates);
      auto* coordinate = static_cast<std::uint32_t*>(wl_array_add(&coordinates, sizeof(std::uint32_t)));
      TEST_CHECK(coordinate != nullptr);
      *coordinate = 10;
      ext_workspace_handle_v1_send_coordinates(workspace, &coordinates);
      wl_array_release(&coordinates);
      ext_workspace_handle_v1_send_state(workspace, EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
      ext_workspace_handle_v1_send_capabilities(workspace, EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
      ext_workspace_manager_v1_send_done(manager);
    }
    explicit WorkspaceServer(int serverFd) {
      TEST_CHECK(display != nullptr);
      TEST_CHECK(wl_global_create(display, &ext_workspace_manager_v1_interface, 1, this, bind) != nullptr);
      TEST_CHECK(wl_client_create(display, serverFd) != nullptr);
      worker = std::jthread([this](std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
          TEST_CHECK(wl_event_loop_dispatch(wl_display_get_event_loop(display), 10) >= 0 || errno == EINTR);
          wl_display_flush_clients(display);
        }
      });
    }
    ~WorkspaceServer() {
      worker.request_stop();
      worker.join();
      wl_display_destroy_clients(display);
      wl_display_destroy(display);
    }
  };
  struct RegistryBinding {
    WaylandWorkspaces& workspaces;
  };
  void global(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t) {
    if (std::strcmp(interface, "ext_workspace_manager_v1") == 0) {
      auto* manager = static_cast<ext_workspace_manager_v1*>(
          wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, 1)
      );
      static_cast<RegistryBinding*>(data)->workspaces.bindExtWorkspace(manager);
    }
  }
  void removed(void*, wl_registry*, std::uint32_t) {}
} // namespace

int main() {
  ::setenv("NOCTALIA_SESSION_ROLE", "secondary", 1);
  ::setenv("XDG_RUNTIME_DIR", "/tmp", 1);
  ::setenv("WAYLAND_DISPLAY", "noctalia-workspace-fixture", 1);
  ::unsetenv("WAYLAND_SOCKET");
  ::setenv("XDG_CURRENT_DESKTOP", "ShojiWM", 1);
  ::setenv("HYPRLAND_INSTANCE_SIGNATURE", "stale", 1);
  TEST_CHECK(compositors::detect() == compositors::CompositorKind::Shoji);
  int fds[2]{};
  TEST_CHECK(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);
  WorkspaceServer server(fds[0]);
  auto* client = wl_display_connect_to_fd(fds[1]);
  TEST_CHECK(client != nullptr);
  {
    compositors::CompositorRuntimeRegistry runtimes;
    WaylandWorkspaces workspaces(runtimes);
    workspaces.initialize();
    TEST_CHECK(workspaces.all().empty()); // no fallback to inherited Hyprland
    auto* registry = wl_display_get_registry(client);
    RegistryBinding binding{workspaces};
    const wl_registry_listener listener{global, removed};
    TEST_CHECK(wl_registry_add_listener(registry, &listener, &binding) == 0);
    TEST_CHECK(wl_display_roundtrip(client) >= 0);
    TEST_CHECK(wl_display_roundtrip(client) >= 0);
    workspaces.initialize();
    TEST_CHECK(std::string(workspaces.backendName()) == "ext-workspace");
    const auto rows = workspaces.all();
    TEST_CHECK(rows.size() == 1);
    TEST_CHECK(rows.front().id == "DP-2:11");
    TEST_CHECK(rows.front().name == "甲");
    TEST_CHECK(rows.front().active);
    TEST_CHECK(rows.front().coordinates == std::vector<std::uint32_t>{10});
    workspaces.activate("DP-2:11");
    TEST_CHECK(wl_display_roundtrip(client) >= 0);
    TEST_CHECK(server.activations.load() == 1);
    TEST_CHECK(server.commits.load() == 1);
    workspaces.cleanup();
    wl_registry_destroy(registry);
  }
  wl_display_disconnect(client);
}
