# Retain the standard devkitPro flags, packaging helpers and tool discovery.
# NX_ROOT was redirected to libounce by the toolchain before loading this file.
include_guard(GLOBAL)
if(OUNCE_SHARED)
    # Use the SDK's shared startup, library search paths and initial-exec TLS.
    # The engine and bundled libraries retain their explicit STATIC targets.
    if(NOT EXISTS "${NX_ROOT}/cmake/Platform/NintendoSwitch.cmake" OR
       NOT EXISTS "${NX_ROOT}/switch_shared.specs")
        message(FATAL_ERROR "OUNCE_SHARED requires libounce 20261123 or newer")
    endif()
    include("${NX_ROOT}/cmake/Platform/NintendoSwitch.cmake")
    set(CMAKE_LINK_GROUP_USING_RESCAN "LINKER:--start-group" "LINKER:--end-group")
    set(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE)
    return()
endif()
include(Platform/NintendoSwitch)
# Newer libounce provides the native CPU flags. Keep the older SDK usable,
# but avoid silently retaining Switch 1's A57 tuning in fresh Switch 2 builds.
if(EXISTS "${NX_ROOT}/switch2_cpu.mk")
    file(STRINGS "${NX_ROOT}/switch2_cpu.mk" STAR_SWITCH2_CPU REGEX "^SWITCH2_CPU[ \t]*:=")
    string(REGEX REPLACE "^SWITCH2_CPU[ \t]*:=[ \t]*" "" STAR_SWITCH2_CPU "${STAR_SWITCH2_CPU}")
    if(NOT STAR_SWITCH2_CPU)
        message(FATAL_ERROR "No SWITCH2_CPU found in ${NX_ROOT}/switch2_cpu.mk")
    endif()
    set(NX_ARCH_SETTINGS "${STAR_SWITCH2_CPU} -mtp=soft -ftls-model=local-exec")
    __dkp_init_platform_settings(NX)
endif()
set(CMAKE_LINK_GROUP_USING_RESCAN "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE)
foreach(lang C CXX ASM)
    string(APPEND CMAKE_${lang}_FLAGS_INIT " -D__SWITCH2__")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L${DEVKITPRO}/portlibs/switch2/lib ${CMAKE_EXE_LINKER_FLAGS_INIT}")
