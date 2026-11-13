# This directory alone selects the SDK's documented newlib opt-out. The
# default SDK benchmark and game keep their normal -lnx/-lounce linkage.
# Separate processes prevent mixing newlib and mimalloc allocation domains.
set(CMAKE_CXX_STANDARD_LIBRARIES "-lounce_core -lm")
add_executable(xallocator-compare-newlib ../StarSwitch2AllocatorCompare.cpp ../StarSwitch2RpmallocCandidate.cpp)
target_include_directories(xallocator-compare-newlib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_compile_definitions(xallocator-compare-newlib PRIVATE STAR_SWITCH2_COMPARE_NEWLIB)
target_link_libraries(xallocator-compare-newlib PRIVATE rpmalloc_switch2_candidate ounce_core pthread)
target_link_options(xallocator-compare-newlib PRIVATE "-Wl,-Map,xallocator-compare-newlib.map")
nx_generate_nacp(OUTPUT xallocator-compare-newlib.nacp NAME "Newlib / rpmalloc baseline" AUTHOR "xStarbound contributors" VERSION "${PROJECT_VERSION}")
nx_create_nro(xallocator-compare-newlib NACP xallocator-compare-newlib.nacp)
