#include "app/instance_context.h"
#include "compositors/compositor_detect.h"
#include "compositors/compositor_platform.h"
#include "compositors/hyprland/hyprland_runtime.h"
#include "compositors/shoji/shoji_identity.h"
#include "tests/test_check.h"
#include "wayland/wayland_connection.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
  int listener(const std::string& path, bool abstract = false) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    TEST_CHECK(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    TEST_CHECK(path.size() < sizeof(address.sun_path));
    std::memcpy(address.sun_path, path.data(), path.size());
    const auto size = abstract ? offsetof(sockaddr_un, sun_path) + path.size() : sizeof(address);
    TEST_CHECK(::bind(fd, reinterpret_cast<sockaddr*>(&address), static_cast<socklen_t>(size)) == 0);
    TEST_CHECK(::listen(fd, 32) == 0);
    return fd;
  }
} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / ("nwc-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root);
  // Instrument the command boundary inside the private fixture. A regression
  // must never reach the owner's loginctl/systemctl or hardware tools.
  ::setenv("PATH", root.c_str(), 1);
  ::setenv("NOCTALIA_EXIT_EFFECTS", (root / "exit-effects").c_str(), 1);
  for (const std::string name : {"loginctl", "systemctl", "labwc", "mmsg", "wlr-randr"}) {
    const auto command = root / name;
    {
      std::ofstream script(command);
      script << "#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$NOCTALIA_EXIT_EFFECTS\"\n";
    }
    std::filesystem::permissions(command, std::filesystem::perms::owner_all);
  }
  const auto wayland = (root / "wayland-1").string();
  const int displayFd = listener(wayland);
  const auto address = compositors::shoji::nativeAddress(wayland);
  const int shojiFd = listener(address, true);
  std::thread responder([&]() {
    const int client = ::accept4(shojiFd, nullptr, nullptr, SOCK_CLOEXEC);
    TEST_CHECK(client >= 0);
    char command[9]{};
    std::size_t used = 0;
    while (used < sizeof(command)) {
      const auto received = ::read(client, command + used, sizeof(command) - used);
      TEST_CHECK(received > 0);
      used += static_cast<std::size_t>(received);
    }
    TEST_CHECK(std::string(command, 9) == "identity\n");
    TEST_CHECK(::send(client, "ok identity\n", 12, MSG_NOSIGNAL) == 12);
    ::close(client);
  });
  ::setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
  ::setenv("WAYLAND_DISPLAY", wayland.c_str(), 1);
  ::setenv("NOCTALIA_SESSION_ROLE", "secondary", 1);
  ::unsetenv("WAYLAND_SOCKET");
  ::setenv("HYPRLAND_INSTANCE_SIGNATURE", "inherited-not-selected", 1);
  ::setenv("XDG_CURRENT_DESKTOP", "Hyprland", 1);
  ::setenv("XDG_SESSION_ID", "outer-login-session", 1);
  ::setenv("USER", "must-not-terminate", 1);
  TEST_CHECK(compositors::detect() == compositors::CompositorKind::Shoji);
  responder.join();
  ::close(shojiFd);
  TEST_CHECK(!compositors::shoji::hasNativeIdentity(wayland));
  TEST_CHECK(
      compositors::shoji::nativeAddress(wayland)
      != compositors::shoji::nativeAddress((root / "nested/wayland-1").string())
  );
  compositors::hyprland::HyprlandRuntime hypr;
  TEST_CHECK(hypr.requestSocketPath().empty());
  WaylandConnection connection;
  CompositorPlatform platform(connection);
  TEST_CHECK(!platform.requestSessionExit());  // no logind/systemctl/user-wide fallback on Shoji
  TEST_CHECK(!platform.setOutputPower(false)); // no invented hardware/wake implementation
  TEST_CHECK(!std::filesystem::exists(root / "exit-effects"));
  TEST_CHECK(noctalia::instance::socketBelongsToWayland(wayland, wayland));
  TEST_CHECK(!noctalia::instance::socketBelongsToWayland((root / "absent.sock").string(), wayland));
  int ready[2]{};
  int release[2]{};
  TEST_CHECK(::pipe(ready) == 0 && ::pipe(release) == 0);
  const auto foreignSocket = (root / "foreign.sock").string();
  const pid_t foreign = ::fork();
  TEST_CHECK(foreign >= 0);
  if (foreign == 0) {
    ::close(ready[0]);
    ::close(release[1]);
    const int foreignFd = listener(foreignSocket);
    TEST_CHECK(::write(ready[1], "r", 1) == 1);
    char stop = 0;
    TEST_CHECK(::read(release[0], &stop, 1) == 1);
    ::close(foreignFd);
    _exit(0);
  }
  ::close(ready[1]);
  ::close(release[0]);
  char started = 0;
  TEST_CHECK(::read(ready[0], &started, 1) == 1);
  TEST_CHECK(!noctalia::instance::socketBelongsToWayland(foreignSocket, wayland));
  TEST_CHECK(::write(release[1], "x", 1) == 1);
  ::close(ready[0]);
  ::close(release[1]);
  int status = 0;
  TEST_CHECK(::waitpid(foreign, &status, 0) == foreign);
  TEST_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  ::close(displayFd);
  std::filesystem::remove_all(root);
}
