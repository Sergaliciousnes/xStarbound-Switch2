if(STAR_ENABLE_STEAM_INTEGRATION)
    add_subdirectory(steam)
endif()

add_library(star_application STATIC
        StarApplication.cpp
        StarApplication.hpp
        StarApplicationController.hpp
        StarMainApplication.hpp
        StarMainApplication_sdl.cpp
        StarP2PNetworkingService_pc.cpp
        StarP2PNetworkingService_pc.hpp
        StarPlatformServices_pc.cpp
        StarPlatformServices_pc.hpp
        StarRenderer.cpp
        StarRenderer.hpp
        StarRenderProfiling.hpp
        StarSdlRenderBackend.cpp
        StarSdlRenderBackend.hpp
        StarTextureAtlas.hpp
        $<$<TARGET_EXISTS:Star::ApplicationSteam>:$<TARGET_OBJECTS:Star::ApplicationSteam>>
)

target_include_directories(star_application
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(star_application
        PUBLIC
        Star::Platform
        Star::Core
        $<$<TARGET_EXISTS:Discord::SDK>:Discord::SDK>
        $<$<TARGET_EXISTS:Star::ApplicationSteam>:Star::ApplicationSteam>
        ${STAR_EXT_GUI_LIBS}
)

target_precompile_headers(star_application REUSE_FROM star_core)

if(STAR_ENABLE_OPENGL)
    target_sources(star_application PRIVATE StarRendererFactory.cpp StarRendererFactory.hpp StarRenderer_opengl20.cpp StarRenderer_opengl20.hpp)
    target_compile_definitions(star_application PRIVATE STAR_ENABLE_OPENGL)
endif()

if(STAR_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    if(STAR_SYSTEM STREQUAL "switch2")
        # Use the SDK Vulkan entry points directly; SDL's desktop loader is unavailable.
        target_link_libraries(star_application PUBLIC Vulkan::Vulkan)
    endif()
    target_sources(star_application PRIVATE StarVulkanDiagnostic.cpp StarVulkanDiagnostic.hpp StarRenderer_vulkan.cpp StarRenderer_vulkan.hpp)
    target_include_directories(star_application SYSTEM PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(star_application PRIVATE STAR_ENABLE_VULKAN)
    find_program(STAR_GLSLANG_VALIDATOR NAMES glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
    set(STAR_VULKAN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/vulkan-generated")
    file(MAKE_DIRECTORY "${STAR_VULKAN_GENERATED_DIR}")
    foreach(STAR_VULKAN_KIND IN ITEMS flat world)
    foreach(STAR_VULKAN_STAGE IN ITEMS vert frag)
        if(STAR_VULKAN_STAGE STREQUAL "vert")
            set(STAR_VULKAN_SUFFIX Vertex)
        else()
            set(STAR_VULKAN_SUFFIX Fragment)
        endif()
        if(STAR_VULKAN_KIND STREQUAL "world")
            set(STAR_VULKAN_SHADER_NAME "StarVulkanWorld${STAR_VULKAN_SUFFIX}")
        else()
            set(STAR_VULKAN_SHADER_NAME "StarVulkanFlat${STAR_VULKAN_SUFFIX}")
        endif()
        set(STAR_VULKAN_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/${STAR_VULKAN_KIND}.${STAR_VULKAN_STAGE}")
        set(STAR_VULKAN_SHADER_HEADER "${STAR_VULKAN_GENERATED_DIR}/${STAR_VULKAN_SHADER_NAME}.hpp")
        add_custom_command(OUTPUT "${STAR_VULKAN_SHADER_HEADER}"
            COMMAND "${STAR_GLSLANG_VALIDATOR}" -V --target-env vulkan1.0 --vn "${STAR_VULKAN_SHADER_NAME}" -o "${STAR_VULKAN_SHADER_HEADER}" "${STAR_VULKAN_SHADER_SOURCE}"
            DEPENDS "${STAR_VULKAN_SHADER_SOURCE}"
            COMMENT "Compiling Vulkan ${STAR_VULKAN_STAGE} shader to embedded SPIR-V"
            VERBATIM)
        target_sources(star_application PRIVATE "${STAR_VULKAN_SHADER_HEADER}")
    endforeach()
    endforeach()
    target_include_directories(star_application PRIVATE "${STAR_VULKAN_GENERATED_DIR}")
    # Entry points are loaded through SDL. Do not add a mandatory Vulkan loader
    # dependency to the executable: OpenGL must still work without Vulkan.
endif()

add_library(Star::Application ALIAS star_application)

if(STAR_SYSTEM_SWITCH2)
    target_compile_definitions(star_application PRIVATE STAR_BUILD_PROFILE="${CMAKE_BUILD_TYPE}")
    if(STAR_SWITCH2_WAIT_PROFILE OR STAR_SWITCH2_IO_PROFILE OR STAR_SWITCH2_ALLOCATION_PROFILE OR STAR_SWITCH2_EXPLORATION_PROFILE)
        set_source_files_properties(StarMainApplication_sdl.cpp PROPERTIES COMPILE_DEFINITIONS STAR_SWITCH2_WAIT_PROFILE)
    endif()
    if(OUNCE_SHARED)
        set_property(SOURCE StarMainApplication_sdl.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_SHARED_RUNTIME)
    endif()
    set_property(SOURCE StarMainApplication_sdl.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_RENDER_HEIGHT=${STAR_SWITCH2_RENDER_HEIGHT})
    if(STAR_SWITCH2_GAME_RPMALLOC AND (STAR_SWITCH2_DIAGNOSTICS OR STAR_SWITCH2_WAIT_PROFILE OR STAR_SWITCH2_IO_PROFILE OR STAR_SWITCH2_ALLOCATION_PROFILE OR STAR_SWITCH2_EXPLORATION_PROFILE OR STAR_SWITCH2_ASSET_STARTUP_TRACE))
        set_property(SOURCE StarMainApplication_sdl.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_RESOURCE_REPORTS)
    endif()
endif()

if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_EXPLORATION_PROFILE)
    set_property(SOURCE StarMainApplication_sdl.cpp StarRenderer_vulkan.cpp StarVulkanDiagnostic.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_EXPLORATION_PROFILE)
endif()

if(STAR_SYSTEM_SWITCH2 AND STAR_ENABLE_VULKAN)
    add_executable(xvulkan-test StarSwitch2VulkanTest.cpp StarSwitch2Benchmarks.cpp)
    set_source_files_properties(StarSwitch2Benchmarks.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
    target_link_libraries(xvulkan-test PRIVATE Star::Application Star::Game)
    nx_generate_nacp(OUTPUT xvulkan-test.nacp NAME "xStarbound Vulkan test" AUTHOR "xStarbound contributors" VERSION "${PROJECT_VERSION}")
    nx_create_nro(xvulkan-test NACP xvulkan-test.nacp)
endif()

# These baselines intentionally select or instrument a static SDK allocator.
# They must not mix ounce_core with the shared libounce runtime.
if(STAR_SYSTEM_SWITCH2 AND NOT OUNCE_SHARED)
    set(STAR_COMPARE_SDK "unknown")
    if(EXISTS "${DEVKITPRO}/ounce-sdk-version.txt")
        file(STRINGS "${DEVKITPRO}/ounce-sdk-version.txt" STAR_COMPARE_SDK LIMIT_COUNT 1)
        string(REPLACE " " "-" STAR_COMPARE_SDK "${STAR_COMPARE_SDK}")
    endif()
    string(REPLACE " " "," STAR_COMPARE_CPU "${NX_ARCH_SETTINGS}")
    configure_file(StarSwitch2AllocatorBuild.hpp.in StarSwitch2AllocatorBuild.hpp @ONLY)
    add_executable(xallocator-compare StarSwitch2AllocatorCompare.cpp StarSwitch2RpmallocCandidate.cpp)
    target_include_directories(xallocator-compare PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    if(EXISTS "${NX_ROOT}/lib/libounce_malloc.a")
        target_compile_definitions(xallocator-compare PRIVATE STAR_SWITCH2_COMPARE_MIMALLOC)
        add_subdirectory(allocator-newlib)
    endif()
    target_link_libraries(xallocator-compare PRIVATE rpmalloc_switch2_candidate nx pthread)
    target_link_options(xallocator-compare PRIVATE "-Wl,-Map,xallocator-compare.map")
    nx_generate_nacp(OUTPUT xallocator-compare.nacp NAME "Allocator comparison" AUTHOR "xStarbound contributors" VERSION "${PROJECT_VERSION}")
    nx_create_nro(xallocator-compare NACP xallocator-compare.nacp)
    add_executable(xallocator-diagnostic StarSwitch2AllocatorDiagnostic.cpp StarSwitch2LockProbe.cpp)
    target_link_libraries(xallocator-diagnostic PRIVATE nx pthread)
    target_link_options(xallocator-diagnostic PRIVATE "-Wl,--wrap=__malloc_lock,--wrap=svcArbitrateLock,--wrap=svcArbitrateUnlock,-Map,xallocator-diagnostic.map")
    nx_generate_nacp(OUTPUT xallocator-diagnostic.nacp NAME "Allocator lock diagnostic" AUTHOR "xStarbound contributors" VERSION "${PROJECT_VERSION}")
    nx_create_nro(xallocator-diagnostic NACP xallocator-diagnostic.nacp)
endif()

if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_GAME_RPMALLOC)
    add_executable(xallocator-routing-test ../test/switch2_game_allocator_test.cpp ../core/StarSwitch2GameAllocator.cpp ../core/StarSwitch2RpmallocGameBackend.cpp ../core/StarSwitch2CppAllocator.cpp ../core/StarSwitch2GameCWrappers.cpp)
    target_link_libraries(xallocator-routing-test PRIVATE rpmalloc_switch2_candidate nx pthread)
    target_link_options(xallocator-routing-test PRIVATE "-Wl,--wrap=free,--wrap=realloc")
    if(OUNCE_SHARED)
        add_library(xallocator-boundary SHARED ../test/switch2_allocator_boundary.cpp)
        set_target_properties(xallocator-boundary PROPERTIES SOVERSION 1)
        ounce_create_library_nro(xallocator-boundary)
        target_link_libraries(xallocator-routing-test PRIVATE xallocator-boundary)
        target_compile_definitions(xallocator-routing-test PRIVATE STAR_SWITCH2_SHARED_BOUNDARY_TEST)
        target_link_options(xallocator-routing-test PRIVATE "-Wl,--export-dynamic-symbol=_Zn*,--export-dynamic-symbol=_Zd*")
    endif()
    nx_generate_nacp(OUTPUT xallocator-routing-test.nacp NAME "Game allocator routing test" AUTHOR "xStarbound contributors" VERSION "${PROJECT_VERSION}")
    nx_create_nro(xallocator-routing-test NACP xallocator-routing-test.nacp)
    set_property(SOURCE StarMainApplication_sdl.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_GAME_RPMALLOC)
    if(TARGET xvulkan-test)
        target_sources(xvulkan-test PRIVATE ../core/StarSwitch2GameCWrappers.cpp)
    endif()
endif()
