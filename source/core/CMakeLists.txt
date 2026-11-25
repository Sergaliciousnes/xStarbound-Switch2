configure_file(StarVersion.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/StarVersion.cpp @ONLY)

add_library(star_core STATIC
        ${CMAKE_CURRENT_BINARY_DIR}/StarVersion.cpp
        StarAStar.hpp
        StarAlgorithm.hpp
        StarArray.hpp
        StarAssetPath.cpp
        StarAssetPath.hpp
        StarAtomicSharedPtr.hpp
        StarAudio.cpp
        StarAudio.hpp
        StarBTree.hpp
        StarBTreeDatabase.cpp
        StarBTreeDatabase.hpp
        StarBiMap.hpp
        StarBlockAllocator.hpp
        StarBuffer.cpp
        StarBuffer.hpp
        StarByteArray.cpp
        StarByteArray.hpp
        StarBytes.hpp
        StarCasting.hpp
        StarColor.cpp
        StarColor.hpp
        StarCompression.cpp
        StarCompression.hpp
        StarConfig.hpp
        StarCurve25519.cpp
        StarCurve25519.hpp
        StarDataStream.cpp
        StarDataStream.hpp
        StarDataStreamDevices.cpp
        StarDataStreamDevices.hpp
        StarDataStreamExtra.hpp
        StarDirectives.cpp
        StarDirectives.hpp
        StarDynamicLib.hpp
        StarEither.hpp
        StarEncode.cpp
        StarEncode.hpp
        StarException.hpp
        StarFile.cpp
        StarFile.hpp
        StarFlatHashMap.hpp
        StarFlatHashSet.hpp
        StarFont.cpp
        StarFont.hpp
        StarFormat.hpp
        StarFormattedJson.cpp
        StarFormattedJson.hpp
        StarHash.hpp
        StarHostAddress.cpp
        StarHostAddress.hpp
        StarIODevice.cpp
        StarIODevice.hpp
        StarIdMap.hpp
        StarImage.cpp
        StarImage.hpp
        StarImageProcessing.cpp
        StarImageProcessing.hpp
        StarInputEvent.cpp
        StarInputEvent.hpp
        StarInterpolation.hpp
        StarIterator.hpp
        StarJson.cpp
        StarJson.hpp
        StarJsonBuilder.cpp
        StarJsonBuilder.hpp
        StarJsonExtra.cpp
        StarJsonExtra.hpp
        StarJsonParser.hpp
        StarJsonPatch.cpp
        StarJsonPatch.hpp
        StarJsonPath.cpp
        StarJsonPath.hpp
        StarJsonRpc.cpp
        StarJsonRpc.hpp
        StarLexicalCast.hpp
        StarLine.hpp
        StarList.hpp
        StarListener.cpp
        StarListener.hpp
        StarLockFile.hpp
        StarLogging.cpp
        StarLogging.hpp
        StarLruCache.hpp
        StarLua.cpp
        StarLua.hpp
        StarLuaConverters.cpp
        StarLuaConverters.hpp
        StarMap.hpp
        StarMathCommon.hpp
        StarMatrix3.hpp
        StarMaybe.hpp
        StarMemory.cpp
        StarMemory.hpp
        StarMultiArray.hpp
        StarMultiArrayInterpolator.hpp
        StarMultiTable.hpp
        StarNetElement.cpp
        StarNetElement.hpp
        StarNetElementBasicFields.cpp
        StarNetElementBasicFields.hpp
        StarNetElementContainers.hpp
        StarNetElementDynamicGroup.hpp
        StarNetElementFloatFields.hpp
        StarNetElementGroup.cpp
        StarNetElementGroup.hpp
        StarNetElementSignal.hpp
        StarNetElementSyncGroup.cpp
        StarNetElementSyncGroup.hpp
        StarNetElementSystem.hpp
        StarNetElementTop.hpp
        StarNetImpl.hpp
        StarObserverStream.hpp
        StarOptionParser.cpp
        StarOptionParser.hpp
        StarOrderedMap.hpp
        StarOrderedSet.hpp
        StarOutputProxy.hpp
        StarParametricFunction.hpp
        StarPch.hpp
        StarPeriodic.hpp
        StarPeriodicFunction.hpp
        StarPerlin.cpp
        StarPerlin.hpp
        StarPoly.hpp
        StarPythonic.hpp
        StarRandom.cpp
        StarRandom.hpp
        StarRandomPoint.hpp
        StarRect.hpp
        StarRefPtr.hpp
        StarRpcPromise.hpp
        StarRpcThreadPromise.hpp
        StarSectorArray2D.hpp
        StarSecureRandom.hpp
        StarSet.hpp
        StarSha256.cpp
        StarSha256.hpp
        StarShellParser.cpp
        StarShellParser.hpp
        StarSignalHandler.hpp
        StarSocket.cpp
        StarSocket.hpp
        StarSpatialHash2D.hpp
        StarSpline.hpp
        StarStaticRandom.hpp
        StarStaticVector.hpp
        StarString.cpp
        StarString.hpp
        StarStringView.cpp
        StarStringView.hpp
        StarStrongTypedef.hpp
        StarTcp.cpp
        StarTcp.hpp
        StarText.cpp
        StarText.hpp
        StarThread.cpp
        StarThread.hpp
        StarTickRateMonitor.cpp
        StarTickRateMonitor.hpp
        StarTime.cpp
        StarTime.hpp
        StarTtlCache.hpp
        StarUdp.cpp
        StarUdp.hpp
        StarUnicode.cpp
        StarUnicode.hpp
        StarUuid.cpp
        StarUuid.hpp
        StarVector.hpp
        StarVersion.hpp
        StarVlqEncoding.hpp
        StarWeightedPool.hpp
        StarWorkerPool.cpp
        StarWorkerPool.hpp
        StarXXHash.hpp
        scripting/StarUtilityLuaBindings.cpp
        scripting/StarUtilityLuaBindings.hpp
)

if(STAR_SYSTEM_FAMILY_UNIX)
    target_sources(star_core
            PRIVATE
            StarException_unix.cpp
            StarFile_unix.cpp
            StarLockFile_unix.cpp
            StarSignalHandler_unix.cpp
            StarThread_unix.cpp
            StarTime_unix.cpp
    )
    if(STAR_SYSTEM_SWITCH2)
        target_sources(star_core PRIVATE StarDynamicLib_switch2.cpp StarSecureRandom_switch2.cpp)
        if(STAR_SWITCH2_WAIT_PROFILE OR STAR_SWITCH2_IO_PROFILE OR STAR_SWITCH2_ALLOCATION_PROFILE OR STAR_SWITCH2_EXPLORATION_PROFILE OR STAR_SWITCH2_DIAGNOSTICS OR STAR_SWITCH2_ASSET_STARTUP_TRACE)
            target_sources(star_core PRIVATE StarSwitch2WaitProfile.cpp)
        else()
            # A clean client must remove the legacy always-compiled timing
            # scopes and buffers, not merely leave recording inactive.
            target_compile_definitions(star_core PUBLIC STAR_SWITCH2_TIMING_DISABLED)
        endif()
        if(STAR_SWITCH2_ALLOCATION_PROFILE)
            target_sources(star_core PRIVATE StarSwitch2AllocationProfile.cpp)
        endif()
        if(STAR_SWITCH2_IO_PROFILE)
            target_sources(star_core PRIVATE StarSwitch2IoProfile.cpp)
            target_compile_definitions(star_core PUBLIC STAR_SWITCH2_IO_PROFILE)
        endif()
        if(STAR_SWITCH2_DIAGNOSTICS)
            target_sources(star_core PRIVATE StarSwitch2Diagnostics.cpp)
        endif()
    else()
        target_sources(star_core PRIVATE StarDynamicLib_unix.cpp StarSecureRandom_unix.cpp)
    endif()
elseif(STAR_SYSTEM_FAMILY_WINDOWS)
    target_sources(star_core
            PUBLIC
            StarString_windows.hpp
            StarDynamicLib_windows.cpp
            StarFile_windows.cpp
            StarLockFile_windows.cpp
            StarSignalHandler_windows.cpp
            StarString_windows.cpp
            StarThread_windows.cpp
            StarTime_windows.cpp
            StarException_windows.cpp
            StarSecureRandom_windows.cpp
    )
endif()

target_include_directories(star_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripting
)

target_link_libraries(star_core
        PUBLIC
        Star::Extern
)

target_precompile_headers(star_core PUBLIC StarPch.hpp)

add_library(Star::Core ALIAS star_core)
if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_EXPLORATION_PROFILE)
    target_sources(star_core PRIVATE StarSwitch2ExplorationProfile.cpp StarSwitch2SimulationProfile.cpp)
    set_property(SOURCE StarSwitch2ExplorationProfile.cpp StarSwitch2SimulationProfile.cpp StarSwitch2WaitProfile.cpp StarLua.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_EXPLORATION_PROFILE)
endif()
if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_ASSET_STARTUP_TRACE)
    target_sources(star_core PRIVATE StarSwitch2AssetStartupTrace.cpp)
    set_property(SOURCE StarSwitch2AssetStartupTrace.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_ASSET_STARTUP_TRACE)
    if(STAR_SWITCH2_GAME_RPMALLOC)
        set_property(SOURCE StarSwitch2AssetStartupTrace.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_GAME_RPMALLOC)
    endif()
endif()
if(STAR_SYSTEM_SWITCH2 AND STAR_SWITCH2_GAME_RPMALLOC)
    target_sources(star_core PRIVATE StarSwitch2GameAllocator.cpp StarSwitch2RpmallocGameBackend.cpp)
    target_link_libraries(star_core PRIVATE "$<LINK_ONLY:rpmalloc_switch2_candidate>")
    target_link_options(star_core INTERFACE "-Wl,--wrap=free,--wrap=realloc")
    if(OUNCE_SHARED)
        # Shared libstdc++ must see the same C++ allocator as the engine.
        # C malloc/free within SDK libraries stay paired with the SDK allocator.
        target_link_options(star_core INTERFACE "-Wl,--export-dynamic-symbol=_Zn*,--export-dynamic-symbol=_Zd*")
    endif()
    set_property(SOURCE StarMemory.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_GAME_RPMALLOC)
    if(STAR_SWITCH2_ALLOCATION_PROFILE)
        set_property(SOURCE StarMemory.cpp APPEND PROPERTY COMPILE_DEFINITIONS STAR_SWITCH2_GAME_ALLOC_PROFILE)
    endif()
endif()
