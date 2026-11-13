#ifndef STAR_VULKAN_DIAGNOSTIC_HPP
#define STAR_VULKAN_DIAGNOSTIC_HPP

#include "StarString.hpp"
#include "StarSdlRenderBackend.hpp"

namespace Star {

// Texture/geometry/presentation bring-up. Does not initialize the game, read its assets,
// or open player/universe storage. Returns nonzero on initialization, rendering,
// or validation failure.
int runVulkanDiagnostic(StringList const& arguments);
// Uses the tested Vulkan device/presentation path with application-owned draws.
unique_ptr<SdlRenderBackend> createSdlVulkanMenuBackend(bool gameplay = false);

}

#endif
