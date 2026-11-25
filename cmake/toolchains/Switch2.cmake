# Use devkitPro's Windows/MSYS2 or Unix CMake, not native Windows CMake.
if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "Set DEVKITPRO to your devkitPro installation")
endif()
set(CMAKE_SYSTEM_NAME NintendoSwitch2)
option(OUNCE_SHARED "Experimentally link the Switch 2 SDK runtime and Vulkan dynamically" OFF)
if(OUNCE_SHARED)
    set(ENV{OUNCE_SHARED} 1)
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OUNCE_SHARED)
set(OUNCE_PORTLIBS "$ENV{DEVKITPRO}/portlibs/switch2")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/..")
include("$ENV{DEVKITPRO}/cmake/Switch.cmake")

# Switch.cmake supplies the compiler and NRO helpers; substitute libounce.
set(NX_ROOT "${DEVKITPRO}/libounce")
set(NX_DEFAULT_ICON "${NX_ROOT}/default_icon.jpg" CACHE FILEPATH "" FORCE)
__dkp_platform_prefix("${DEVKITPRO}/portlibs/switch2" "${NX_ROOT}")
set(STAR_SYSTEM switch2 CACHE STRING "Target operating system")
set(STAR_SYSTEM_FAMILY unix CACHE STRING "POSIX platform implementations")
set(STAR_ARCHITECTURE aarch64 CACHE STRING "Target architecture")
set(STAR_LITTLE_ENDIAN TRUE CACHE BOOL "Target byte order")
set(Vulkan_INCLUDE_DIR "${DEVKITPRO}/portlibs/switch2/include" CACHE PATH "Target Vulkan headers")
# MSYS CMake writes Unix absolute includes into PCH files which native GCC
# cannot resolve. Ordinary compiler arguments are converted by MSYS correctly.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "MSYS")
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "Disable incompatible MSYS PCH paths")
endif()
