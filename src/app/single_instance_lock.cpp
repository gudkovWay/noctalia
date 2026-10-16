#include "app/single_instance_lock.h"

#include "app/instance_context.h"
#include "core/log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>

namespace {
  constexpr Logger kLog("instance");
} // namespace

SingleInstanceLock::~SingleInstanceLock() { release(); }

bool SingleInstanceLock::tryAcquire() {
  m_path = resolveLockPath();

  const int fd = ::open(m_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (noctalia::instance::current().isolated()) {
      throw std::runtime_error("cannot open per-instance lock: " + std::string(std::strerror(errno)));
    }
    // The lock file is unusable (e.g. read-only runtime dir). Degrade to running
    // unguarded rather than refusing to start — losing the single-instance
    // guarantee is better than a shell that won't launch.
    kLog.warn("could not open lock file {}: {}; running without single-instance guard", m_path, std::strerror(errno));
    return true;
  }

  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    if (errno == EWOULDBLOCK) {
      // Another live instance holds the lock.
      ::close(fd);
      return false;
    }
    // Unexpected flock failure — degrade to unguarded, same rationale as above.
    if (noctalia::instance::current().isolated()) {
      const std::string reason = std::strerror(errno);
      ::close(fd);
      throw std::runtime_error("cannot acquire per-instance lock: " + reason);
    }
    kLog.warn("flock failed on {}: {}; running without single-instance guard", m_path, std::strerror(errno));
    ::close(fd);
    return true;
  }

  m_fd = fd;
  if (noctalia::instance::current().isolated()) {
    // Bridge the ordinary legacy address without exposing a legacy IPC alias.
    // This prevents a legacy shell and an opt-in shell owning the same display.
    const auto& context = noctalia::instance::current();
    const std::string compat =
        context.runtimeDir + "/noctalia-" + std::filesystem::path(context.waylandSocket).filename().string() + ".lock";
    m_compatFd = ::open(compat.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_compatFd < 0) {
      release();
      throw std::runtime_error("cannot open compatible per-display lock");
    }
    if (::flock(m_compatFd, LOCK_EX | LOCK_NB) != 0) {
      const int error = errno;
      release();
      if (error == EWOULDBLOCK)
        return false;
      throw std::runtime_error("cannot acquire compatible per-display lock");
    }
  }
  return true;
}

void SingleInstanceLock::release() noexcept {
  if (m_compatFd >= 0) {
    ::close(m_compatFd);
    m_compatFd = -1;
  }
  if (m_fd >= 0) {
    // Closing the fd releases the flock. The lock file itself is intentionally
    // left in place: unlinking it would let a racing instance flock a different
    // inode than a third instance later creates, defeating the lock.
    ::close(m_fd);
    m_fd = -1;
  }
}

std::string SingleInstanceLock::resolveLockPath() { return noctalia::instance::current().lockPath(); }
