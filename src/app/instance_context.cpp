#include "app/instance_context.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glib.h>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace noctalia::instance {
  namespace {
    std::string_view env(const char* name) {
      const char* value = std::getenv(name);
      return value != nullptr ? std::string_view(value) : std::string_view{};
    }
    void publish(const char* name, const std::string& value) {
      if (::setenv(name, value.c_str(), 1) != 0) {
        throw std::runtime_error(std::string("cannot export ") + name);
      }
    }
    std::optional<ucred> peer(std::string_view path) {
      if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
        return std::nullopt;
      const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
      if (fd < 0)
        return std::nullopt;
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      std::memcpy(address.sun_path, path.data(), path.size());
      bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
      if (!connected && errno == EINPROGRESS) {
        pollfd pending{fd, POLLOUT, 0};
        int error = 0;
        socklen_t size = sizeof(error);
        connected =
            ::poll(&pending, 1, 200) > 0 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
      }
      ucred credentials{};
      socklen_t size = sizeof(credentials);
      const bool ok = connected && ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0;
      ::close(fd);
      if (!ok || credentials.pid <= 1 || credentials.uid != ::getuid())
        return std::nullopt;
      return credentials;
    }
  } // namespace

  std::string_view Context::roleName() const noexcept {
    switch (role) {
    case Role::Legacy:
      return "legacy";
    case Role::Primary:
      return "primary";
    case Role::Secondary:
      return "secondary";
    }
    return "legacy";
  }

  std::string Context::socketPath(std::string_view prefix, std::string_view suffix) const {
    if (isolated()) {
      // Abstract endpoints keep full-path identities usable even when the
      // canonical Wayland directory leaves no room for a filesystem IPC name.
      return "@" + std::string(prefix) + "-" + std::to_string(::getuid()) + "-" + id + std::string(suffix);
    }
    return runtimeDir + "/" + std::string(prefix) + "-" + legacyDisplay + std::string(suffix);
  }

  std::string Context::lockPath() const {
    return runtimeDir + "/noctalia-" + (isolated() ? id : legacyDisplay) + ".lock";
  }

  std::string Context::statePath(std::string_view base) const {
    if (base.empty()) {
      if (isolated())
        throw std::runtime_error("multi-session state root is unavailable");
      return {};
    }
    if (isolated()) {
      if (!std::filesystem::path(base).is_absolute())
        throw std::runtime_error("multi-session state root must be absolute");
      return std::string(base) + "/sessions/" + id;
    }
    return std::string(base);
  }

  Context resolve(std::string_view role, std::string_view display, std::string_view runtime) {
    Context result;
    if (role.empty()) {
      result.runtimeDir = runtime.empty() ? "/tmp" : std::string(runtime);
      result.legacyDisplay = display.empty() ? "wayland-0" : std::string(display);
      return result;
    }
    if (role == "primary")
      result.role = Role::Primary;
    else if (role == "secondary")
      result.role = Role::Secondary;
    else
      throw std::runtime_error("NOCTALIA_SESSION_ROLE / --session-role must be primary or secondary");
    if (display.empty())
      throw std::runtime_error("multi-session mode requires WAYLAND_DISPLAY");
    const std::filesystem::path displayPath(display);
    if (!displayPath.is_absolute() && (runtime.empty() || !std::filesystem::path(runtime).is_absolute())) {
      throw std::runtime_error("relative WAYLAND_DISPLAY requires an absolute XDG_RUNTIME_DIR");
    }
    if (displayPath.filename().empty()
        || displayPath.filename() == "."
        || displayPath.filename() == ".."
        || display.find('\0') != std::string_view::npos) {
      throw std::runtime_error("invalid WAYLAND_DISPLAY");
    }
    if (!displayPath.is_absolute()) {
      for (const auto& component : displayPath) {
        if (component == "..")
          throw std::runtime_error("relative WAYLAND_DISPLAY must not escape XDG_RUNTIME_DIR");
      }
    }
    std::error_code error;
    if (!displayPath.is_absolute()) {
      result.runtimeDir = std::filesystem::canonical(runtime, error).string();
      if (error)
        throw std::runtime_error("XDG_RUNTIME_DIR cannot be canonicalized: " + error.message());
    }
    result.waylandSocket =
        std::filesystem::weakly_canonical(
            displayPath.is_absolute() ? displayPath : std::filesystem::path(result.runtimeDir) / displayPath, error
        )
            .string();
    if (error)
      throw std::runtime_error("WAYLAND_DISPLAY cannot be canonicalized: " + error.message());
    if (std::filesystem::is_directory(result.waylandSocket, error)) {
      throw std::runtime_error("WAYLAND_DISPLAY names a directory, not a socket");
    }
    // Address the selected socket, not the caller's potentially different
    // runtime root. Absolute/relative aliases must share IPC AND lock selection.
    result.runtimeDir = std::filesystem::path(result.waylandSocket).parent_path().string();
    gchar* digest = g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, reinterpret_cast<const guchar*>(result.waylandSocket.data()), result.waylandSocket.size()
    );
    if (digest == nullptr)
      throw std::runtime_error("cannot derive Noctalia instance identity");
    result.id = digest;
    g_free(digest);
    return result;
  }

  const Context& current() {
    static const Context context = []() {
      if (!env("NOCTALIA_SESSION_ROLE").empty() && !env("WAYLAND_SOCKET").empty()) {
        throw std::runtime_error(
            "multi-session mode cannot identify an inherited WAYLAND_SOCKET fd; use WAYLAND_DISPLAY"
        );
      }
      return resolve(env("NOCTALIA_SESSION_ROLE"), env("WAYLAND_DISPLAY"), env("XDG_RUNTIME_DIR"));
    }();
    return context;
  }

  void consumeOptions(int& argc, char** argv) {
    for (int i = 1; i < argc && argv[i][0] == '-'; ++i) {
      if (std::string_view(argv[i]) != "--session-role")
        continue;
      if (i + 1 >= argc
          || (std::string_view(argv[i + 1]) != "primary" && std::string_view(argv[i + 1]) != "secondary")) {
        throw std::runtime_error("--session-role requires primary or secondary");
      }
      publish("NOCTALIA_SESSION_ROLE", argv[i + 1]);
      for (int j = i; j + 2 < argc; ++j)
        argv[j] = argv[j + 2];
      argc -= 2;
      argv[argc] = nullptr;
      --i;
    }
  }

  void exportSubprocessContext(std::string_view stateDir) {
    const auto& context = current();
    if (!context.isolated()) {
      // These are derived outputs, never configuration inputs. Clearing the
      // role in a nested launch must not leave a parent's isolated context.
      constexpr const char* outputs[]{
          "NOCTALIA_INSTANCE_ID", "NOCTALIA_WAYLAND_SOCKET", "NOCTALIA_IPC_SOCKET", "NOCTALIA_SESSION_STATE_DIR",
          "NOCTALIA_BINARY"
      };
      for (const char* name : outputs) {
        if (std::getenv(name) != nullptr && ::unsetenv(name) != 0) {
          throw std::runtime_error(std::string("cannot clear inherited ") + name);
        }
      }
      return;
    }
    publish("WAYLAND_DISPLAY", context.waylandSocket);
    publish("NOCTALIA_INSTANCE_ID", context.id);
    publish("NOCTALIA_WAYLAND_SOCKET", context.waylandSocket);
    publish("NOCTALIA_IPC_SOCKET", context.socketPath("noctalia", ".sock"));
    publish("NOCTALIA_SESSION_STATE_DIR", std::string(stateDir));
    std::error_code error;
    const auto binary = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error)
      throw std::runtime_error("cannot resolve Noctalia executable context");
    publish("NOCTALIA_BINARY", binary.string());
  }

  bool socketBelongsToWayland(std::string_view socketPath, std::string_view waylandSocket) {
    const auto selected = peer(waylandSocket);
    const auto candidate = peer(socketPath);
    return selected && candidate && selected->pid == candidate->pid && selected->uid == candidate->uid;
  }
} // namespace noctalia::instance
