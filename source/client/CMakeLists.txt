if(xstarbound_VERSION_PATCH STREQUAL "")
    set(xstarbound_VERSION_PATCH "0")
endif()
if(xstarbound_VERSION_TWEAK STREQUAL "")
    set(xstarbound_VERSION_TWEAK "0")
endif()

set(xstarbound_RC_VERSION "${xstarbound_VERSION_MAJOR},${xstarbound_VERSION_MINOR},${xstarbound_VERSION_PATCH},${xstarbound_VERSION_TWEAK}")
configure_file(xclient.rc.in ${CMAKE_CURRENT_BINARY_DIR}/xclient.rc @ONLY)

add_executable(xclient WIN32
        StarClientApplication.cpp
        StarClientApplication.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/xclient.rc
)

target_precompile_headers(xclient REUSE_FROM star_core)

# Write xclient PDB to top-level build dir for easy archiving
set_target_properties(xclient PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/PDB/$<CONFIG>"
)

target_link_libraries(xclient
        PRIVATE
        Star::Frontend
)

if(STAR_SYSTEM_SWITCH2)
    nx_generate_nacp(OUTPUT xclient.nacp NAME "xStarbound" AUTHOR "Sergalicious" VERSION "${PROJECT_VERSION}")
    nx_create_nro(xclient NACP xclient.nacp ICON "${CMAKE_CURRENT_SOURCE_DIR}/xclient-switch2.jpg")
endif()

if(STAR_COMPILER_GNU AND STAR_CROSS_COMPILE)
    # `-mwindows` stops cross-compiled xClient from opening up a console window when started.
    target_link_options(xclient BEFORE PUBLIC "-mwindows")
endif()

# The active config runs against source-tree assets. Also provide a clearly
# named install config for users manually copying the executable into a game
# installation; copying the active build config would retain source-tree paths.
add_custom_command(TARGET xclient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/xsbinit.config.build" "$<TARGET_FILE_DIR:xclient>/xsbinit.config"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/xsbinit.config" "$<TARGET_FILE_DIR:xclient>/xsbinit.config.install"
)

# Also copy Steam/Discord SDK libraries over to the build dir on Windows. UNIX has RPATH for this.
if (STAR_SYSTEM_WINDOWS)
    if(STAR_ENABLE_DISCORD_INTEGRATION)
        add_custom_command(TARGET xclient POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy "${STAR_DISCORD_SDK_LIBRARY_FILE}" "$<TARGET_FILE_DIR:xclient>"
        )
    endif()
    if(STAR_ENABLE_STEAM_INTEGRATION)
        add_custom_command(TARGET xclient POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy "${STAR_STEAM_SDK_LIBRARY_FILE}" "$<TARGET_FILE_DIR:xclient>"
        )
    endif()
endif()

if(STAR_SYSTEM_SWITCH2)
    install(FILES "$<TARGET_FILE_DIR:xclient>/xclient.nro"
            DESTINATION ${STAR_INSTALL_BINDIR} COMPONENT Client)
else()
install(TARGETS xclient
        RUNTIME_DEPENDENCY_SET STAR_RUNTIME_DEPS
        RUNTIME DESTINATION ${STAR_INSTALL_BINDIR}
        COMPONENT Client
)

# Install any linked, non-system libraries alongside the game executable.
# This will include vcpkg-provided libraries, as they're residing in the binary dir.
string(REGEX REPLACE "([][+.*()^])" "\\\\\\1" _regex_source_dir "${PROJECT_SOURCE_DIR}")
string(REGEX REPLACE "([][+.*()^])" "\\\\\\1" _regex_binary_dir "${PROJECT_BINARY_DIR}")
install(RUNTIME_DEPENDENCY_SET STAR_RUNTIME_DEPS
        PRE_EXCLUDE_REGEXES
            "libSDL2-2\.0\.so.*" # Only debug SDL2 library will be installed on Linux!
            "api-ms-" "ext-ms-" # Virtual Windows runtime DLLs
        POST_INCLUDE_REGEXES "^${_regex_source_dir}/client/.+" "^${_regex_binary_dir}/client/.+"
        POST_EXCLUDE_REGEXES ".*"
        LIBRARY DESTINATION ${STAR_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${STAR_INSTALL_LIBDIR}
        FRAMEWORK DESTINATION ${STAR_INSTALL_LIBDIR}
        COMPONENT Libraries
)

endif()

if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_GAMEPLAY_REGRESSION)
    set_source_files_properties(StarClientApplication.cpp PROPERTIES COMPILE_DEFINITIONS STAR_SWITCH2_GAMEPLAY_REGRESSION)
endif()

if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_ALLOCATION_PROFILE)
    target_sources(xclient PRIVATE ../core/StarSwitch2HeapWait.cpp)
    target_link_options(xclient PRIVATE "-Wl,--wrap=__malloc_lock")
    target_sources(xclient PRIVATE ../core/StarSwitch2AllocationWrappers.cpp)
    target_link_options(xclient PRIVATE "-Wl,--wrap=_Znwm,--wrap=_Znam,--wrap=_ZdlPv,--wrap=_ZdaPv,--wrap=_ZdlPvm,--wrap=_ZdaPvm,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free")
endif()

if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_GAME_RPMALLOC)
    target_sources(xclient PRIVATE ../core/StarSwitch2CppAllocator.cpp)
    set_property(SOURCE StarClientApplication.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_GAME_RPMALLOC)
    set_property(SOURCE ../core/StarSwitch2AllocationWrappers.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_GAME_RPMALLOC)
    target_link_options(xclient PRIVATE "-Wl,-Map,xclient-rpmalloc.map")
    if(NOT STAR_SWITCH2_ALLOCATION_PROFILE)
        target_sources(xclient PRIVATE ../core/StarSwitch2GameCWrappers.cpp)
    endif()
endif()

if(STAR_ENABLE_STEAM_INTEGRATION)
    install(FILES
            ${PROJECT_SOURCE_DIR}/scripts/steam_appid.txt
            DESTINATION ${STAR_INSTALL_BINDIR}
            COMPONENT Client
    )
endif()
