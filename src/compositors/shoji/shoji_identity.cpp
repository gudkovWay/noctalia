#include "compositors/shoji/shoji_identity.h"

#include "app/instance_context.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace compositors::shoji {
  std::string nativeAddress(std::string_view path) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : path) {
      hash = (hash ^ static_cast<unsigned char>(byte)) * 1099511628211ULL;
    }
    std::string address(1, '\0');
    address += std::format("shojiwm-output-power-{}-{:016x}", ::getuid(), hash);
    return address;
  }

  bool hasNativeIdentity(std::string_view path) {
    if (path.empty())
      return false;
    const auto name = nativeAddress(path);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
      return false;
    struct Close {
      int fd;
      ~Close() { ::close(fd); }
    } close{fd};
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (name.size() >= sizeof(address.sun_path))
      return false;
    std::memcpy(address.sun_path, name.data(), name.size());
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + name.size());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), length) != 0)
      return false;
    constexpr std::string_view command = "identity\n";
    if (::send(fd, command.data(), command.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(command.size()))
      return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    char reply[64]{};
    std::size_t used = 0;
    while (used < sizeof(reply)) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0)
        return false;
      pollfd pending{fd, POLLIN, 0};
      const int ready = ::poll(&pending, 1, static_cast<int>(remaining));
      if (ready < 0 && errno == EINTR)
        continue;
      if (ready <= 0)
        return false;
      const ssize_t received = ::recv(fd, reply + used, sizeof(reply) - used, 0);
      if (received < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      if (received <= 0)
        return false;
      used += static_cast<std::size_t>(received);
      const std::string_view text(reply, used);
      const auto newline = text.find('\n');
      if (newline != std::string_view::npos)
        return text.substr(0, newline) == "ok identity";
    }
    return false;
  }

  bool selectedInstanceIsShoji() {
    const char* display = std::getenv("WAYLAND_DISPLAY");
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (display == nullptr)
      return false;
    try {
      const auto context = noctalia::instance::resolve("secondary", display, runtime != nullptr ? runtime : "");
      return hasNativeIdentity(context.waylandSocket);
    } catch (const std::runtime_error&) {
      return false;
    }
  }
} // namespace compositors::shoji
