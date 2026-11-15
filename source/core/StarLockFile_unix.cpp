#include "StarLockFile.hpp"
#include "StarTime.hpp"
#include "StarThread.hpp"
#ifdef STAR_SYSTEM_SWITCH2
#include "StarLogging.hpp"
#include "StarFile.hpp"
#include "StarProcessLock.hpp"
#include <sys/stat.h>
#endif

#ifndef STAR_SYSTEM_SWITCH2
#include <sys/file.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

namespace Star {

int64_t const LockFile::MaximumSleepMillis;

Maybe<LockFile> LockFile::acquireLock(String const& filename, int64_t lockTimeout) {
  LockFile lock(std::move(filename));
  if (lock.lock(lockTimeout))
    return lock;
  return {};
}

LockFile::LockFile(String const& filename) : m_filename(std::move(filename)) {}

LockFile::LockFile(LockFile&& lockFile) {
  operator=(std::move(lockFile));
}

LockFile::~LockFile() {
  unlock();
}

LockFile& LockFile::operator=(LockFile&& lockFile) {
  m_filename = std::move(lockFile.m_filename);
  m_handle = std::move(lockFile.m_handle);

  return *this;
}

bool LockFile::lock(int64_t timeout) {
#ifdef STAR_SYSTEM_SWITCH2
  if (m_handle) return true;
  auto key = ProcessLock::pathKey(m_filename.utf8(), File::currentDirectory().utf8());
  auto doFLock = [&key](String const& filename, bool) -> shared_ptr<void> {
    auto lease = ProcessLock::tryAcquire(key);
    if (!lease) return {};
    // Old releases created empty O_EXCL markers. Remove only those legacy
    // empty regular files, after acquiring the in-process ownership guard.
    // A leftover marker or cleanup error must not block the next game launch.
    struct stat info{};
    if (::stat(filename.utf8Ptr(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 0) {
      int result = ::unlink(filename.utf8Ptr());
      int error = errno;
      try {
        if (result == 0)
          Logger::info("Removed obsolete SD lock marker '{}'", filename);
        else if (error != ENOENT)
          Logger::warn("Could not remove obsolete SD lock marker '{}': {}; using process-local ownership", filename, strerror(error));
      } catch (...) {}
    }
    return lease;
  };
#else
  auto doFLock = [](String const& filename, bool block) -> shared_ptr<int> {
    int fd = open(filename.utf8Ptr(), O_RDONLY | O_CREAT, 0644);
    if (fd < 0)
      throw StarException(strf("Could not open lock file {}, {}\n", filename, strerror(errno)));

    int ret;
    if (block)
      ret = flock(fd, LOCK_EX);
    else
      ret = flock(fd, LOCK_EX | LOCK_NB);

    if (ret != 0) {
      close(fd);
      if (errno != EWOULDBLOCK)
        throw StarException(strf("Could not lock file {}, {}\n", filename, strerror(errno)));
      return {};
    }

    return make_shared<int>(fd);
  };
#endif

  if (timeout < 0) {
#ifdef STAR_SYSTEM_SWITCH2
    while (!(m_handle = doFLock(m_filename, false)))
      Thread::sleep(MaximumSleepMillis);
#else
    m_handle = doFLock(m_filename, true);
#endif
    return true;
  } else if (timeout == 0) {
    m_handle = doFLock(m_filename, false);
    return (bool)m_handle;
  } else {
    int64_t startTime = Time::monotonicMilliseconds();
    while (true) {
      m_handle = doFLock(m_filename, false);
      if (m_handle)
        return true;

      if (Time::monotonicMilliseconds() - startTime > timeout)
        return false;

      Thread::sleep(min(timeout / 4, MaximumSleepMillis));
    }
  }
}

void LockFile::unlock() {
#ifdef STAR_SYSTEM_SWITCH2
  m_handle.reset();
#else
  if (m_handle) {
    int fd = *(int*)m_handle.get();
    unlink(m_filename.utf8Ptr());
    close(fd);
    m_handle.reset();
  }
#endif
}

bool LockFile::isLocked() const {
  return (bool)m_handle;
}

}
