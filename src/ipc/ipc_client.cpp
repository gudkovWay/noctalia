#include "ipc/ipc_client.h"

#include "app/instance_context.h"
#include "ipc/unix_socket_address.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

  constexpr char kCallerCwdSeparator = '\x1e';

  std::string resolveSocketPath() { return noctalia::instance::current().socketPath("noctalia", ".sock"); }

} // namespace

int IpcClient::send(const std::string& command) {
  const std::string path = resolveSocketPath();

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    std::println(stderr, "error: socket() failed: {}", std::strerror(errno));
    return 1;
  }

  // Set connect/send/recv timeout to 2 seconds
  timeval tv{};
  tv.tv_sec = 2;
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  const auto addr = noctalia::ipc::unixSocketAddress(path);
  if (!addr) {
    std::println(stderr, "error: socket address too long");
    ::close(fd);
    return 1;
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr->address), addr->size) < 0) {
    std::println(stderr, "error: noctalia is not running");
    ::close(fd);
    return 1;
  }

  // Prefix the caller cwd so the daemon resolves relative paths correctly.
  std::string line;
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec && cwd.is_absolute()) {
    line = cwd.string();
    line += kCallerCwdSeparator;
  }
  line += command;

  std::size_t sent = 0;
  while (sent < line.size()) {
    const auto written = ::write(fd, line.data() + sent, line.size() - sent);
    if (written < 0) {
      std::println(stderr, "error: write() failed: {}", std::strerror(errno));
      ::close(fd);
      return 1;
    }
    sent += static_cast<std::size_t>(written);
  }

  if (::shutdown(fd, SHUT_WR) < 0) {
    std::println(stderr, "error: shutdown() failed: {}", std::strerror(errno));
    ::close(fd);
    return 1;
  }

  // Read response until EOF (server closes connection after writing)
  std::string response;
  char buf[4096];
  for (;;) {
    const auto n = ::read(fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::println(stderr, "error: read() failed: {}", std::strerror(errno));
      ::close(fd);
      return 1;
    }
    if (n == 0) {
      break;
    }
    response.append(buf, static_cast<std::size_t>(n));
  }

  ::close(fd);

  std::fputs(response.c_str(), stdout);

  // Return 1 if the response indicates an error
  return (response.starts_with("error:")) ? 1 : 0;
}
