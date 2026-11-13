#include "StarEncode.hpp"
#include "StarFile.hpp"
#include "StarSwitch2IoProfile.hpp"
#include "StarFormat.hpp"
#include "StarRandom.hpp"
#include "StarSwitch2Diagnostics.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef STAR_SYSTEM_SWITCH2
#include <switch.h>
#include "StarFileReplacement.hpp"
#include "StarLogging.hpp"
#include <mutex>
#endif

#ifdef STAR_SYSTEM_MACOSX
#include <mach-o/dyld.h>
#elif defined STAR_SYSTEM_FREEBSD
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace Star {

namespace {
#ifdef STAR_SYSTEM_SWITCH2
  struct Switch2FileHandle {
    int fd;
    std::string removeOnClose;
  };

  int fdFromHandle(void* ptr) {
    if (!ptr) throw IOException("File operation on closed File");
    return static_cast<Switch2FileHandle*>(ptr)->fd;
  }

  void* handleFromFd(int fd, char const* removeOnClose = nullptr) {
    try {
      return new Switch2FileHandle{fd, removeOnClose ? removeOnClose : ""};
    } catch (...) {
      ::close(fd);
      if (removeOnClose) ::unlink(removeOnClose);
      throw;
    }
  }

  String temporaryBaseDirectory() {
    static std::mutex directoryMutex;
    std::lock_guard<std::mutex> guard(directoryMutex);
    String directory = "sdmc:/switch/xstarbound/storage/tmp";
    if (!File::isDirectory(directory)) File::makeDirectoryRecursive(directory);
    return directory;
  }
#else
  int fdFromHandle(void* ptr) {
    return (int)(intptr_t)ptr;
  }

  void* handleFromFd(int handle) {
    return (void*)(intptr_t)handle;
  }

  String temporaryBaseDirectory() { return P_tmpdir; }
#endif
} // namespace

String File::convertDirSeparators(String const& path) {
  return path.replace("\\", "/");
}

String File::currentDirectory() {
  char buffer[PATH_MAX];
  if (::getcwd(buffer, PATH_MAX) == NULL)
    throw IOException("getcwd failed");

  return String(buffer);
}

void File::changeDirectory(const String& dirName) {
  if (::chdir(dirName.utf8Ptr()) != 0)
    throw IOException(strf("could not change directory to {}", dirName));
}

void File::makeDirectory(String const& dirName) {
  if (::mkdir(dirName.utf8Ptr(), 0777) != 0)
    throw IOException(strf("could not create directory '{}', {}", dirName, strerror(errno)));
}

List<pair<String, bool>> File::dirList(const String& dirName, bool skipDots) {
  List<std::pair<String, bool>> fileList;
  DIR* directory = ::opendir(dirName.utf8Ptr());
  if (directory == NULL)
    throw IOException::format("dirList failed on dir: '{}'", dirName);

  for (dirent* entry = ::readdir(directory); entry != NULL; entry = ::readdir(directory)) {
    String entryString = entry->d_name;
    if (!skipDots || (entryString != "." && entryString != "..")) {
      bool isDirectory = false;
      if (entry->d_type == DT_DIR) {
        isDirectory = true;
      } else if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
        isDirectory = File::isDirectory(File::relativeTo(dirName, entryString));
      }
      fileList.append({entryString, isDirectory});
    }
  }
  ::closedir(directory);

  return fileList;
}

String File::baseName(const String& fileName) {
#ifdef STAR_SYSTEM_SWITCH2
  auto path = fileName.utf8();
  while (path.size() > 1 && path.back() == '/' && path[path.size() - 2] != ':') path.pop_back();
  if (path.empty()) return ".";
  if (path == "/" || path.back() == '/') return "/";
  auto slash = path.find_last_of('/');
  return String(slash == std::string::npos ? path : path.substr(slash + 1));
#else
  String ret;

  std::string file = fileName.utf8();
  char* fn = new char[file.size() + 1];
  std::copy(file.begin(), file.end(), fn);
  fn[file.size()] = 0;
  ret = String(::basename(fn));
  delete[] fn;

  return ret;
#endif
}

String File::dirName(const String& fileName) {
#ifdef STAR_SYSTEM_SWITCH2
  auto path = fileName.utf8();
  while (path.size() > 1 && path.back() == '/' && path[path.size() - 2] != ':') path.pop_back();
  if (path.empty()) return ".";
  if (path.back() == '/') return String(path);
  auto slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0 || path[slash - 1] == ':') return String(path.substr(0, slash + 1));
  return String(path.substr(0, slash));
#else
  String ret;

  std::string file = fileName.utf8();
  char* fn = new char[file.size() + 1];
  std::copy(file.begin(), file.end(), fn);
  fn[file.size()] = 0;
  ret = String(::dirname(fn));
  delete[] fn;

  return ret;
#endif
}

String File::relativeTo(String const& relativeTo, String const& path) {
#ifdef STAR_SYSTEM_SWITCH2
  // Preserve device paths such as sdmc:/ and romfs:/ as absolute paths.
  auto utf8 = path.utf8();
  auto mount = utf8.find(":/");
  if (mount != std::string::npos && utf8.find('/') == mount + 1)
    return path;
#endif
  if (path.beginsWith("/"))
    return path;
  return relativeTo.trimEnd("/") + '/' + path;
}

String File::fullPath(const String& fileName) {
  char buffer[PATH_MAX] = {};

  if (::realpath(fileName.utf8Ptr(), buffer) == NULL)
    throw IOException::format("realpath failed on file: '{}' problem path was: '{}'", fileName, buffer);

  return String(buffer);
}

String File::temporaryFileName() {
  return relativeTo(temporaryBaseDirectory(), strf("starbound.tmpfile.{}", hexEncode(Random::randBytes(16))));
}

FilePtr File::temporaryFile() {
  return open(temporaryFileName(), IOMode::ReadWrite);
}

FilePtr File::ephemeralFile() {
  auto file = make_shared<File>();
  ByteArray path = ByteArray::fromCStringWithNull(relativeTo(temporaryBaseDirectory(), "starbound.tmpfile.XXXXXXXX").utf8Ptr());
  auto res = mkstemp(path.ptr());
  if (res < 0)
    throw IOException::format("Could not create temporary file '{}': {}", path.ptr(), strerror(errno));
#ifdef STAR_SYSTEM_SWITCH2
  // Horizon need not allow unlinking an open file. Keep the temporary name
  // private and delete it after closing the native file on close/destruction.
  file->m_file = handleFromFd(res, path.ptr());
#else
  if (::unlink(path.ptr()) < 0) {
    int unlinkError = errno;
    ::close(res);
    throw IOException::format("Could not remove mkstemp file when creating ephemeralFile: {}", strerror(unlinkError));
  }
  file->m_file = handleFromFd(res);
#endif
#ifdef STAR_SWITCH2_IO_PROFILE
  file->m_ioProfileFile = Switch2IoProfile::file(path.ptr());
#endif
  file->setMode(IOMode::ReadWrite);
  return file;
}

String File::temporaryDirectory() {
  String dirname = relativeTo(temporaryBaseDirectory(), strf("starbound.tmpdir.{}", hexEncode(Random::randBytes(16))));
  makeDirectory(dirname);
  return dirname;
}

bool File::exists(String const& path) {
  struct stat st_buf;
  int status = stat(path.utf8Ptr(), &st_buf);
  return status == 0;
}

bool File::isFile(String const& path) {
  struct stat st_buf;
  int status = stat(path.utf8Ptr(), &st_buf);
  if (status != 0)
    return false;

  return S_ISREG(st_buf.st_mode);
}

bool File::isDirectory(String const& path) {
  struct stat st_buf;
  int status = stat(path.utf8Ptr(), &st_buf);
  if (status != 0)
    return false;

  return S_ISDIR(st_buf.st_mode);
}

void File::remove(String const& filename) {
  if (::remove(filename.utf8Ptr()) < 0)
    throw IOException::format("remove error: {}", strerror(errno));
}

void File::rename(String const& source, String const& target) {
#ifdef STAR_SYSTEM_SWITCH2
  // Serialize the reserve/promote/rollback sequence between engine writers.
  static std::mutex replacementMutex;
  std::lock_guard<std::mutex> guard(replacementMutex);
  struct Operations {
    FileReplaceError rename(String const& from, String const& to) {
      if (::rename(from.utf8Ptr(), to.utf8Ptr()) == 0) return {};
      int error = errno;
      return {error, fsdevGetLastResult()};
    }
    bool isFile(String const& path) { return File::isFile(path); }
    FileReplaceError remove(String const& path) {
      if (::remove(path.utf8Ptr()) == 0) return {};
      int error = errno;
      return {error, fsdevGetLastResult()};
    }
  } operations;
  String backup = target + ".replace-backup-" + hexEncode(Random::randBytes(8));
  auto result = replaceFileByRename(source, target, backup, operations);
  if (result.status == FileReplaceStatus::Complete) return;
  if (result.status == FileReplaceStatus::CompleteWithBackup) {
    Logger::warn("Replaced '{}' but could not remove old backup '{}': {} (Horizon FS 0x{:08x})",
        target, backup, strerror(result.error.code), result.error.native);
    return;
  }
  throw IOException::format("Could not rename '{}' to '{}': {} (errno {}, Horizon FS 0x{:08x}, stage {}, backup '{}', rollback errno {}, rollback FS 0x{:08x})",
      source, target, strerror(result.error.code), result.error.code, result.error.native,
      static_cast<int>(result.status), backup, result.recoveryError.code, result.recoveryError.native);
#else
  if (::rename(source.utf8Ptr(), target.utf8Ptr()) < 0)
    throw IOException::format("Could not rename '{}' to '{}': {}", source, target, strerror(errno));
#endif
}

void File::overwriteFileWithRename(char const* data, size_t len, String const& filename, String const& newSuffix) {
  String newFile = filename + newSuffix;
#ifdef STAR_SYSTEM_SWITCH2
  // Flush the complete staged file before moving the previous destination.
  auto staged = File::open(newFile, IOMode::Write | IOMode::Truncate);
  staged->writeFull(data, len);
  staged->sync();
  staged->close();
#else
  writeFile(data, len, newFile);
#endif
  File::rename(newFile, filename);
}

void* File::fopen(char const* filename, IOMode mode) {
  int oflag = 0;

  if (mode & IOMode::Read && mode & IOMode::Write)
    oflag |= O_RDWR | O_CREAT;
  else if (mode & IOMode::Read)
    oflag |= O_RDONLY;
  else if (mode & IOMode::Write)
    oflag |= O_WRONLY | O_CREAT;

  if (mode & IOMode::Truncate)
    oflag |= O_TRUNC;

  int fd = ::open(filename, oflag, 0666);
  if (fd < 0) {
#ifdef STAR_SYSTEM_SWITCH2
    int openError = errno;
    Result nativeResult = fsdevGetLastResult();
    throw IOException::format("Error opening file '{}', error: {} (errno {}, last Horizon FS result 0x{:08x}, open flags 0x{:x})",
        filename, strerror(openError), openError, nativeResult, oflag);
#else
    throw IOException::format("Error opening file '{}', error: {}", filename, strerror(errno));
#endif
  }

  if (mode & IOMode::Append) {
    if (lseek(fd, 0, SEEK_END) < 0) {
      int seekError = errno;
      ::close(fd);
      throw IOException::format("Error opening file '{}', cannot seek: {}", filename, strerror(seekError));
    }
  }

  return handleFromFd(fd);
}

void File::fseek(void* f, StreamOffset offset, IOSeek seekMode) {
  auto fd = fdFromHandle(f);
  // Thanks to @firobe for discovering this one.
  ssize_t retCode;
  if (seekMode == IOSeek::Relative)
    retCode = lseek(fd, offset, SEEK_CUR);
  else if (seekMode == IOSeek::Absolute)
    retCode = lseek(fd, offset, SEEK_SET);
  else
    retCode = lseek(fd, offset, SEEK_END);

  if (retCode < 0)
    throw IOException::format("Seek error: {}", strerror(errno));
}

StreamOffset File::ftell(void* f) {
  return lseek(fdFromHandle(f), 0, SEEK_CUR);
}

size_t File::fread(void* file, char* data, size_t len) {
  if (len == 0)
    return 0;

  auto fd = fdFromHandle(file);
  auto ret = ::read(fd, data, len);
  Switch2Diagnostics::count(Switch2Diagnostics::Counter::Reads);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EINTR)
      return 0;
    throw IOException::format("Read error: {}", strerror(errno));
  } else {
    return ret;
  }
}

size_t File::fwrite(void* file, char const* data, size_t len) {
  if (len == 0)
    return 0;

  auto fd = fdFromHandle(file);
  auto ret = ::write(fd, data, len);
  Switch2Diagnostics::count(Switch2Diagnostics::Counter::Writes);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EINTR)
      return 0;
    throw IOException::format("Write error: {}", strerror(errno));
  } else {
    return ret;
  }
}

void File::fsync(void* file) {
  auto fd = fdFromHandle(file);
#ifdef STAR_SYSTEM_LINUX
  ::fdatasync(fd);
#else
  ::fsync(fd);
#endif
}

void File::fclose(void* file) {
  if (::close(fdFromHandle(file)) < 0)
    throw IOException::format("Close error: {}", strerror(errno));
#ifdef STAR_SYSTEM_SWITCH2
  auto handle = static_cast<Switch2FileHandle*>(file);
  auto temporaryPath = std::move(handle->removeOnClose);
  delete handle;
  if (!temporaryPath.empty() && ::unlink(temporaryPath.c_str()) < 0 && errno != ENOENT) {
    // The descriptor is already closed: don't throw and leave File holding a
    // freed handle. A leftover temporary name can be cleaned up after exit.
    int cleanupError = errno;
    try {
      Logger::warn("Could not remove closed temporary file '{}': {}", temporaryPath, strerror(cleanupError));
    } catch (...) {}
  }
#endif
}

StreamOffset File::fsize(void* file) {
  StreamOffset pos = ftell(file);
  StreamOffset size = lseek(fdFromHandle(file), 0, SEEK_END);
  lseek(fdFromHandle(file), pos, SEEK_SET);
  return size;
}

size_t File::pread(void* file, char* data, size_t len, StreamOffset position) {
#ifdef STAR_SYSTEM_SWITCH2
  // The File instance holds its cursor mutex for the whole operation.
  auto original = ftell(file);
  if (original < 0) throw IOException::format("Could not query file position: {}", strerror(errno));
  fseek(file, position, IOSeek::Absolute);
  try {
    auto count = fread(file, data, len);
    fseek(file, original, IOSeek::Absolute);
    return count;
  } catch (...) {
    ::lseek(fdFromHandle(file), original, SEEK_SET);
    throw;
  }
#else
  return ::pread(fdFromHandle(file), data, len, position);
#endif
}

size_t File::pwrite(void* file, char const* data, size_t len, StreamOffset position) {
#ifdef STAR_SYSTEM_SWITCH2
  auto original = ftell(file);
  if (original < 0) throw IOException::format("Could not query file position: {}", strerror(errno));
  fseek(file, position, IOSeek::Absolute);
  try {
    auto count = fwrite(file, data, len);
    fseek(file, original, IOSeek::Absolute);
    return count;
  } catch (...) {
    ::lseek(fdFromHandle(file), original, SEEK_SET);
    throw;
  }
#else
  return ::pwrite(fdFromHandle(file), data, len, position);
#endif
}

void File::resize(void* f, StreamOffset size) {
  if (::ftruncate(fdFromHandle(f), size) < 0)
    throw IOException::format("resize error: {}", strerror(errno));
}

} // namespace Star
