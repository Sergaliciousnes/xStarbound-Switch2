#ifndef STAR_MAIN_APPLICATION_HPP
#define STAR_MAIN_APPLICATION_HPP

#include "StarApplication.hpp"
#include "StarApplicationController.hpp"
#include "StarRenderer.hpp"

namespace Star {
  int runMainApplication(ApplicationUPtr application, StringList cmdLineArgs);
}

#if defined STAR_SYSTEM_WINDOWS

#include <windows.h>

#if defined STAR_USE_RPMALLOC

#include "rpmalloc.h"

#define STAR_MAIN_APPLICATION(ApplicationClass)                                            \
  int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {                                \
    ::rpmalloc_initialize(0);                                                              \
    int nArgs;                                                                             \
    LPWSTR* argsList = CommandLineToArgvW(GetCommandLineW(), &nArgs);                      \
    Star::StringList args;                                                                 \
    for (int i = 0; i < nArgs; ++i) args.append(Star::String(argsList[i]));                \
    int returnVal = Star::runMainApplication(Star::make_unique<ApplicationClass>(), args); \
    ::rpmalloc_finalize();                                                                 \
    return returnVal;                                                                      \
  }


#else

#define STAR_MAIN_APPLICATION(ApplicationClass)                                   \
  int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {                       \
    int nArgs;                                                                    \
    LPWSTR* argsList = CommandLineToArgvW(GetCommandLineW(), &nArgs);             \
    Star::StringList args;                                                        \
    for (int i = 0; i < nArgs; ++i) args.append(Star::String(argsList[i]));       \
    return Star::runMainApplication(Star::make_unique<ApplicationClass>(), args); \
  }

#endif

#else

#if defined(STAR_SYSTEM_SWITCH2) && defined(STAR_SWITCH2_GAME_RPMALLOC)
#include "StarSwitch2GameAllocator.hpp"
#ifdef STAR_SWITCH2_TIMING_DISABLED
// Retain an actionable startup error without enabling allocator snapshots.
#define STAR_SWITCH2_ALLOCATOR_FAILURE() do { \
    if (FILE* allocatorFailureLog=std::fopen("sdmc:/xstarbound-stderr.log","w")) { \
      std::fputs("xStarbound: game allocator initialization failed.\n",allocatorFailureLog); \
      std::fclose(allocatorFailureLog); \
    } \
  } while(0)
#define STAR_SWITCH2_ALLOCATOR_REPORT_EXIT() ((void)0)
#else
#define STAR_SWITCH2_ALLOCATOR_FAILURE() Star::Switch2GameAllocator::reportFile("initialization-failed",false)
#define STAR_SWITCH2_ALLOCATOR_REPORT_EXIT() Star::Switch2GameAllocator::reportFile("after-run",true)
#endif
#define STAR_MAIN_APPLICATION(ApplicationClass) \
  int main(int argc,char** argv){ \
    if(!Star::Switch2GameAllocator::initialize()){STAR_SWITCH2_ALLOCATOR_FAILURE();return 1;} \
    int result=Star::runMainApplication(Star::make_unique<ApplicationClass>(),Star::StringList(argc,argv)); \
    STAR_SWITCH2_ALLOCATOR_REPORT_EXIT();return result; \
  }
#elif defined STAR_USE_RPMALLOC

#include "rpmalloc.h"

#define STAR_MAIN_APPLICATION(ApplicationClass)                                                                    \
  int main(int argc, char** argv) {                                                                                \
    ::rpmalloc_initialize(0);                                                                                      \
    int returnVal = Star::runMainApplication(Star::make_unique<ApplicationClass>(), Star::StringList(argc, argv)); \
    ::rpmalloc_finalize();                                                                                         \
    return returnVal;                                                                                              \
  }

#else

#define STAR_MAIN_APPLICATION(ApplicationClass)                                                           \
  int main(int argc, char** argv) {                                                                       \
    return Star::runMainApplication(Star::make_unique<ApplicationClass>(), Star::StringList(argc, argv)); \
  }

#endif

#endif

#endif
