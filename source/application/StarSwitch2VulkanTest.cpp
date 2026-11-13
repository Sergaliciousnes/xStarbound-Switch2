#include "StarVulkanDiagnostic.hpp"
#include "StarSwitch2Benchmarks.hpp"
#include "StarLogging.hpp"
#include "StarFile.hpp"
#include "StarSecureRandom.hpp"
#include "StarEncode.hpp"
#include "StarLockFile.hpp"
#include "../test/btree_cache_regression.hpp"
#include "../test/gameplay_allocation_regression.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace {
void checkRuntime() {
  Star::checkGameplayAllocationRegression();
  Star::Logger::info("Runtime test: empty-item isolation, net-state byte compatibility, streamed star ordering and inline texture lifetimes passed");
  Star::checkBTreeReadCacheRegression();
  Star::Logger::info("Runtime test: B-tree cache warm reads, write visibility, commit, rollback, deletion, reopen and disable passed");
  u64 memoryLimit = 0;
  if (R_SUCCEEDED(svcGetInfo(&memoryLimit, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
    Star::Logger::info("Switch 2: process memory limit {} MiB, {} allowed CPU cores", memoryLimit / (1024 * 1024), Star::Thread::numberOfProcessors());

  // WorldStorage and initial ship generation need real, seekable temporary
  // files. Check concurrent lifetimes and deletion after close/destruction.
  auto directory = Star::File::temporaryDirectory();
  Star::File::removeDirectoryRecursive(directory);
  auto tempDirectory = Star::File::dirName(directory);
  auto lockPath = directory + ".lock";
  {
    auto lock = Star::LockFile::acquireLock(lockPath, 0);
    if (!lock || Star::LockFile::acquireLock(lockPath, 0))
      throw Star::IOException("Runtime test: temporary lock exclusivity failed");
  }
  if (Star::File::exists(lockPath))
    throw Star::IOException("Runtime test: lock file was not removed after release");
  auto beforeTemps = Star::File::dirList(tempDirectory).size();
  auto temporary = Star::File::ephemeralFile();
  {
    auto second = Star::File::ephemeralFile();
    if (temporary->write("shipworld", 9) != 9 || second->write("other", 5) != 5)
      throw Star::IOException("Runtime test: short temporary-file write");
    temporary->seek(2);
    char data[4];
    if (temporary->readAbsolute(0, data, 4) != 4 || std::memcmp(data, "ship", 4) || temporary->pos() != 2
        || temporary->size() != 9 || !temporary->fileName().empty())
      throw Star::IOException("Runtime test: temporary-file contents/cursor/size mismatch");
    temporary->resize(16);
    temporary->sync();
    if (temporary->size() != 16 || second->size() != 5)
      throw Star::IOException("Runtime test: temporary-file resize or isolation failed");
  }
  temporary->close();
  if (Star::File::dirList(tempDirectory).size() != beforeTemps)
    throw Star::IOException("Runtime test: temporary files were not removed after close/destruction");
  Star::Logger::info("Runtime test: SD temporary files, seek/read, resize, independent handles, close/destructor cleanup and lock release passed");

  // Exercise the positional-I/O fallback against a shared moving cursor.
  auto path = "xvulkan-io-" + Star::hexEncode(Star::secureRandomBytes(8)) + ".tmp";
  auto file = Star::File::open(path, Star::IOMode::ReadWrite | Star::IOMode::Truncate);
  try {
    if (file->write("0123456789abcdef", 16) != 16)
      throw Star::IOException("Runtime test: short SD write");
    auto reader = Star::Thread::invoke("SD positional IO", [file]() {
      try {
        for (int i = 0; i < 128; ++i) {
          char data[4];
          if (file->readAbsolute(8, data, sizeof(data)) != sizeof(data) || std::memcmp(data, "89ab", 4)) return false;
        }
        return true;
      } catch (...) { return false; }
    });
    bool cursorValid = true;
    for (int i = 0; i < 128; ++i) {
      file->seek(1);
      char data[2];
      if (file->read(data, sizeof(data)) != sizeof(data) || std::memcmp(data, "12", 2)) cursorValid = false;
    }
    if (!reader.finish() || !cursorValid)
      throw Star::IOException("Runtime test: concurrent positional SD read failed");
    file->seek(3);
    char data[4];
    if (file->writeAbsolute(12, "WXYZ", 4) != 4 || file->pos() != 3
        || file->readAbsolute(12, data, 4) != 4 || std::memcmp(data, "WXYZ", 4) || file->pos() != 3)
      throw Star::IOException("Runtime test: positional SD write/cursor restoration failed");
    file->close();
    Star::File::remove(path);
    Star::Logger::info("Runtime test: concurrent SD reads, positional writes, randomness and engine threads passed");
  } catch (...) {
    file->close();
    Star::File::remove(path);
    throw;
  }
}
}

// Asset-free hardware test of the actual game renderer. Unlike the desktop
// smoke sequence, this does not attempt to resize/minimize a console window.
int main(int, char**) {
  std::freopen("sdmc:/xstarbound-vulkan-startup.log", "w", stdout);
  std::freopen("sdmc:/xstarbound-vulkan-stderr.log", "w", stderr);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  Star::Logger::refreshLoggable();
  if (::chdir("sdmc:/switch/xstarbound") != 0) {
    Star::Logger::error("Create sdmc:/switch/xstarbound before running this test");
    return 1;
  }
  try {
    auto benchmarkLog = Star::make_shared<Star::FileLogSink>("sdmc:/xstarbound-benchmark.log", Star::LogLevel::Info, true);
    Star::Logger::addSink(benchmarkLog);
    Star::Logger::info("[benchmark] suite-v2: bounded CPU, memory/allocation, scratch-file SD and offscreen Vulkan tests; no assets/saves, clocks or priorities changed");
    checkRuntime();
    if (!Star::runSwitch2CpuBenchmarks()) {
      Star::Logger::info("[benchmark] CPU phase cancelled");
      return 0;
    }
    if (!Star::runSwitch2MemoryBenchmarks()) {
      Star::Logger::info("[benchmark] memory phase cancelled");
      return 0;
    }
    if (!Star::runSwitch2SdBenchmarks()) {
      Star::Logger::error("[benchmark] SD phase did not complete; inspect the preceding error/cancellation");
      return 1;
    }
    auto status = Star::runVulkanDiagnostic({"xvulkan-test", "--vulkan-world", "--vulkan-verify", "--vulkan-frames=180", "--vulkan-benchmark"});
    Star::Logger::info("[benchmark] suite finished status={}", status);
    Star::Logger::removeSink(benchmarkLog);
    return status;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "Renderer test startup failed: %s\n", error.what());
    return 1;
  }
}
