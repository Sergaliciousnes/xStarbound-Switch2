#include "StarProcessLock.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
using Star::ProcessLock;
int main(int argc, char** argv) {
  if (argc==3 && std::string(argv[1])=="--abrupt-child") {
    auto lease=ProcessLock::tryAcquire("forced-close-universe");
    assert(lease);
    std::ofstream marker(argv[2],std::ios::binary); assert(marker); marker.close();
    std::_Exit(0); // Deliberately bypass C++ destructors, only in this child.
  }
  auto key=ProcessLock::pathKey("./storage/universe/universe.lock","sdmc:/switch/xstarbound");
  assert(key=="sdmc:/switch/xstarbound/storage/universe/universe.lock");
  assert(key==ProcessLock::pathKey("SDMC:/switch//xstarbound/storage/../storage/universe/./UNIVERSE.LOCK","ignored"));
  assert(key==ProcessLock::pathKey("/switch/xstarbound/storage/universe/universe.lock","sdmc:/other"));
  assert(key!=ProcessLock::pathKey("romfs:/switch/xstarbound/storage/universe/universe.lock","ignored"));
  auto first=ProcessLock::tryAcquire(key);assert(first && !ProcessLock::tryAcquire(key));
  auto retained=first;first.reset();assert(!ProcessLock::tryAcquire(key));
  auto other=ProcessLock::tryAcquire("other-universe");assert(other);
  other=std::move(retained);assert(ProcessLock::tryAcquire("other-universe"));assert(!ProcessLock::tryAcquire(key));
  other.reset();assert(ProcessLock::tryAcquire(key));
  try { auto lease=ProcessLock::tryAcquire(key); assert(lease); throw std::runtime_error("fixture"); } catch(std::runtime_error const&) {}
  assert(ProcessLock::tryAcquire(key));
  std::atomic<int> attempts{0},winners{0};std::vector<std::thread> threads;
  for(int i=0;i<16;++i) threads.emplace_back([&]{
    auto lease=ProcessLock::tryAcquire("concurrent-universe");if(lease)++winners;
    ++attempts;while(attempts.load()!=16)std::this_thread::yield();
  });
  for(auto& thread:threads)thread.join();assert(winners==1);assert(ProcessLock::tryAcquire("concurrent-universe"));
#ifdef _WIN32
  auto marker=std::filesystem::current_path()/"build-switch2/stale-lock-audit"/("stale-marker-"+std::to_string(GetCurrentProcessId())+".lock");
  assert(!std::filesystem::exists(marker));
  char executable[32768]{};assert(GetModuleFileNameA(nullptr,executable,sizeof(executable)));
  std::string command="\""+std::string(executable)+"\" --abrupt-child \""+marker.string()+"\"";
  STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
  assert(CreateProcessA(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
  auto wait=WaitForSingleObject(child.hProcess,10000);
  if(wait!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,1);WaitForSingleObject(child.hProcess,1000);}
  DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hThread);CloseHandle(child.hProcess);
  assert(wait==WAIT_OBJECT_0 && code==0);assert(std::filesystem::is_regular_file(marker) && std::filesystem::file_size(marker)==0);
  auto recovered=ProcessLock::tryAcquire("forced-close-universe");assert(recovered);assert(!ProcessLock::tryAcquire("forced-close-universe"));
  std::filesystem::remove(marker);
#endif
  std::puts("Process lock tests passed: path aliases, exclusive ownership, moves, exception cleanup, concurrent acquisition and abrupt-exit stale marker recovery.");
}
