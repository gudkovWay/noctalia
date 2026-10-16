#pragma once

#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace noctalia::ipc {
  struct UnixSocketAddress {
    sockaddr_un address{};
    socklen_t size = sizeof(sockaddr_un);
  };

  // Leading '@' is the printable form of a Linux abstract address. Legacy
  // filesystem endpoints keep their original encoding and lifetime.
  [[nodiscard]] inline std::optional<UnixSocketAddress> unixSocketAddress(std::string_view path) {
    UnixSocketAddress result;
    if (path.empty() || path.size() >= sizeof(result.address.sun_path))
      return std::nullopt;
    result.address.sun_family = AF_UNIX;
    if (path.front() == '@') {
      std::memcpy(result.address.sun_path + 1, path.data() + 1, path.size() - 1);
      result.size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
    } else {
      std::memcpy(result.address.sun_path, path.data(), path.size());
    }
    return result;
  }

  [[nodiscard]] inline bool unixSocketPeerIsCurrentUser(int fd) {
    ucred peer{};
    socklen_t size = sizeof(peer);
    return ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.uid == ::getuid();
  }
} // namespace noctalia::ipc
