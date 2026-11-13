#include "StarLockFile.hpp"
#include "StarTime.hpp"
#include "StarThread.hpp"
#ifdef STAR_SYSTEM_SWITCH2
#include "StarLogging.hpp"
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
  auto doFLock = [](String const& filename, bool block) -> shared_ptr<int> {
#ifdef STAR_SYSTEM_SWITCH2
    // The SD filesystem has no flock. An exclusive sentinel fails closed;
    // after a crash its stale lock must be removed before reopening storage.
    int fd = open(filename.utf8Ptr(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
      if (errno == EEXIST) return {};
      throw StarException::format("Could not create lock file {}: {}", filename, strerror(errno));
    }
    return make_shared<int>(fd);
#else
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
#endif
  };

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
  if (m_handle) {
    int fd = *(int*)m_handle.get();
#ifdef STAR_SYSTEM_SWITCH2
    // The SD filesystem may reject removal while the file is open.
    int result = close(fd);
    int error = errno;
    if (result == 0) {
      result = unlink(m_filename.utf8Ptr());
      error = errno;
    }
    if (result < 0 && error != ENOENT) {
      try {
        Logger::warn("Could not release SD lock '{}': {}. After the title exits, a stale lock may need removal.", m_filename, strerror(error));
      } catch (...) {}
    }
#else
    unlink(m_filename.utf8Ptr());
    close(fd);
#endif
    m_handle.reset();
  }
}

bool LockFile::isLocked() const {
  return (bool)m_handle;
}

}
