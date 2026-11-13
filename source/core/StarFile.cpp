#include "StarSwitch2WaitProfile.hpp"
#include "StarSwitch2IoProfile.hpp"
#include "StarFile.hpp"
#include "StarSwitch2FrameProfile.hpp"
#include "StarFormat.hpp"

#include <fstream>

namespace Star {

#ifdef STAR_SWITCH2_IO_PROFILE
#define STAR_FILE_IO(op, requested, offset) Switch2IoProfile::Operation profileIo(m_ioProfileFile, Switch2IoProfile::Op::op, requested, offset)
#else
#define STAR_FILE_IO(op, requested, offset) Switch2IoProfile::Operation profileIo(0, Switch2IoProfile::Op::op, requested, offset)
#endif

#ifdef STAR_SYSTEM_SWITCH2
#define STAR_FILE_CURSOR_LOCK Switch2WaitProfile::Scope cursorWait(Switch2WaitProfile::Metric::FileCursor); std::lock_guard<std::recursive_mutex> cursorLock(m_positionMutex); cursorWait.stop()
#else
#define STAR_FILE_CURSOR_LOCK
#endif

void File::makeDirectoryRecursive(String const& fileName) {
  auto parent = dirName(fileName);
  if (!isDirectory(parent))
    makeDirectoryRecursive(parent);
  if (!isDirectory(fileName))
    makeDirectory(fileName);
}

void File::removeDirectoryRecursive(String const& fileName) {
  {
    String fileInDir;
    bool isDir;

    for (auto const& p : dirList(fileName)) {
      std::tie(fileInDir, isDir) = p;

      fileInDir = relativeTo(fileName, fileInDir);

      if (isDir)
        removeDirectoryRecursive(fileInDir);
      else
        remove(fileInDir);
    }
  }

  remove(fileName);
}

void File::copy(String const& source, String const& target) {
  auto sourceFile = File::open(source, IOMode::Read);
  auto targetFile = File::open(target, IOMode::ReadWrite);

  targetFile->resize(0);

  char buf[1024];
  while (!sourceFile->atEnd()) {
    size_t r = sourceFile->read(buf, 1024);
    targetFile->writeFull(buf, r);
  }
}

FilePtr File::open(const String& filename, IOMode mode) {
  auto file = make_shared<File>(filename);
  file->open(mode);
  return file;
}

ByteArray File::readFile(String const& filename) {
  FilePtr file = File::open(filename, IOMode::Read);
  ByteArray bytes;
  while (!file->atEnd()) {
    char buffer[1024];
    size_t r = file->read(buffer, 1024);
    bytes.append(buffer, r);
  }

  return bytes;
}

String File::readFileString(String const& filename) {
  FilePtr file = File::open(filename, IOMode::Read);
  std::string str;
  while (!file->atEnd()) {
    char buffer[1024];
    size_t r = file->read(buffer, 1024);
    for (size_t i = 0; i < r; ++i)
      str.push_back(buffer[i]);
  }

  return str;
}

StreamOffset File::fileSize(String const& filename) {
  return File::open(filename, IOMode::Read)->size();
}

void File::writeFile(char const* data, size_t len, String const& filename) {
  FilePtr file = File::open(filename, IOMode::Write | IOMode::Truncate);
  file->writeFull(data, len);
}

void File::writeFile(ByteArray const& data, String const& filename) {
  writeFile(data.ptr(), data.size(), filename);
}

void File::writeFile(String const& data, String const& filename) {
  writeFile(data.utf8Ptr(), data.utf8Size(), filename);
}

void File::overwriteFileWithRename(ByteArray const& data, String const& filename, String const& newSuffix) {
  overwriteFileWithRename(data.ptr(), data.size(), filename, newSuffix);
}

void File::overwriteFileWithRename(String const& data, String const& filename, String const& newSuffix) {
  overwriteFileWithRename(data.utf8Ptr(), data.utf8Size(), filename, newSuffix);
}

void File::backupFileInSequence(String const& initialFile, String const& targetFile, unsigned maximumBackups, String const& backupExtensionPrefix) {
  for (unsigned i = maximumBackups; i > 0; --i) {
    bool initial = i == 1;
    String const& sourceFile = initial ? initialFile : targetFile;
    String curExtension = initial ? "" : strf("{}{}", backupExtensionPrefix, i - 1);
    String nextExtension = strf("{}{}", backupExtensionPrefix, i);

    if (File::isFile(sourceFile + curExtension))
      File::copy(sourceFile + curExtension, targetFile + nextExtension);
  }
}

void File::backupFileInSequence(String const& targetFile, unsigned maximumBackups, String const& backupExtensionPrefix) {
  backupFileInSequence(targetFile, targetFile, maximumBackups, backupExtensionPrefix);
}

File::File()
  : IODevice(IOMode::Closed) {
  m_file = 0;
}

File::File(String filename)
  : IODevice(IOMode::Closed), m_filename(std::move(filename)), m_file(0) {}

File::~File() {
  close();
}

StreamOffset File::pos() {
  STAR_FILE_IO(Tell, 0, -1);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  if (!m_file)
    throw IOException("pos called on closed File");

  auto result = ftell(m_file);
  profileIo.complete();
  return result;
}

void File::seek(StreamOffset offset, IOSeek seekMode) {
  STAR_FILE_IO(Seek, 0, offset);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  if (!m_file)
    throw IOException("seek called on closed File");

  fseek(m_file, offset, seekMode);
  profileIo.complete();
}

StreamOffset File::size() {
  STAR_FILE_IO(Size, 0, -1);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  auto result = fsize(m_file);
  profileIo.complete();
  return result;
}

bool File::atEnd() {
  STAR_FILE_IO(Eof, 0, -1);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  if (!m_file)
    throw IOException("eof called on closed File");

  bool result = ftell(m_file) >= fsize(m_file);
  profileIo.complete();
  return result;
}

size_t File::read(char* data, size_t len) {
  STAR_FILE_IO(Read, len, -1);
  Switch2FrameProfile::Zone readZone(Switch2FrameProfile::Phase::FileRead);
  Switch2WaitProfile::Scope ioScope(Switch2WaitProfile::Metric::FileRead);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  if (!m_file)
    throw IOException("read called on closed File");

  if (!isReadable())
    throw IOException("read called on non-readable File");

  auto result = fread(m_file, data, len);
  profileIo.complete(result);
  return result;
}

size_t File::write(const char* data, size_t len) {
  STAR_FILE_IO(Write, len, -1);
  Switch2FrameProfile::Zone writeZone(Switch2FrameProfile::Phase::FileWrite);
  Switch2WaitProfile::Scope ioScope(Switch2WaitProfile::Metric::FileWrite);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  if (!m_file)
    throw IOException("write called on closed File");

  if (!isWritable())
    throw IOException("write called on non-writable File");

  auto result = fwrite(m_file, data, len);
  profileIo.complete(result);
  return result;
}

size_t File::readAbsolute(StreamOffset readPosition, char* data, size_t len) {
  STAR_FILE_IO(ReadAbsolute, len, readPosition);
  Switch2FrameProfile::Zone readZone(Switch2FrameProfile::Phase::FileRead);
  Switch2WaitProfile::Scope ioScope(Switch2WaitProfile::Metric::FileRead);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  auto result = pread(m_file, data, len, readPosition);
  profileIo.complete(result);
  return result;
}

size_t File::writeAbsolute(StreamOffset writePosition, char const* data, size_t len) {
  STAR_FILE_IO(WriteAbsolute, len, writePosition);
  Switch2FrameProfile::Zone writeZone(Switch2FrameProfile::Phase::FileWrite);
  Switch2WaitProfile::Scope ioScope(Switch2WaitProfile::Metric::FileWrite);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  auto result = pwrite(m_file, data, len, writePosition);
  profileIo.complete(result);
  return result;
}

String File::fileName() const {
  return m_filename;
}

void File::setFilename(String filename) {
  if (isOpen())
    throw IOException("Cannot call setFilename while File is open");
  m_filename = std::move(filename);
}

void File::remove() {
  close();
  if (m_filename.empty())
    throw IOException("Cannot remove file, no filename set");
  remove(m_filename);
}

void File::resize(StreamOffset s) {
  STAR_FILE_IO(Resize, s, -1);
  STAR_FILE_CURSOR_LOCK;
  profileIo.acquired();
  bool tempOpen = false;
  if (!isOpen()) {
    tempOpen = true;
    open(mode());
  }

  File::resize(m_file, s);

  if (tempOpen)
    close();
  profileIo.complete();
}

void File::sync() {
  STAR_FILE_IO(Sync, 0, -1);
  Switch2FrameProfile::Zone syncZone(Switch2FrameProfile::Phase::FileSync);
  Switch2WaitProfile::Scope ioScope(Switch2WaitProfile::Metric::FileSync);
  if (!m_file)
    throw IOException("sync called on closed File");

  fsync(m_file);
  profileIo.complete();
}

void File::open(IOMode m) {
  close();
  if (m_filename.empty())
    throw IOException("Cannot open file, no filename set");

#ifdef STAR_SWITCH2_IO_PROFILE
  m_ioProfileFile = Switch2IoProfile::file(m_filename.utf8Ptr());
#endif
  STAR_FILE_IO(Open, 0, -1);
  m_file = fopen(m_filename.utf8Ptr(), m);
  setMode(m);
  profileIo.complete();
}

void File::close() {
  if (m_file) {
    STAR_FILE_IO(Close, 0, -1);
    fclose(m_file);
    profileIo.complete();
  }
  m_file = 0;
  setMode(IOMode::Closed);
}

String File::deviceName() const {
  if (m_filename.empty())
    return "<unnamed temp file>";
  else
    return m_filename;
}

}
