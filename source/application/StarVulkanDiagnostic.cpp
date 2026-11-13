#include "StarSwitch2WaitProfile.hpp"
#include "StarSwitch2ExplorationProfile.hpp"
#if defined(__SWITCH2__)
#include <switch.h>
#define VK_USE_PLATFORM_VI_NN 1
#else
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "SDL_vulkan.h"

#include "StarVulkanDiagnostic.hpp"
#include "StarSwitch2FrameProfile.hpp"
#include "StarSdlRenderBackend.hpp"
#include "StarLogging.hpp"
#include "StarSwitch2Diagnostics.hpp"
#include "StarRenderer_vulkan.hpp"
#include "StarRenderProfiling.hpp"
#include "StarVulkanFlatVertex.hpp"
#include "StarVulkanFlatFragment.hpp"
#include "StarVulkanWorldVertex.hpp"
#include "StarVulkanWorldFragment.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <vector>
#include <map>
#include <chrono>
#include "StarPerformanceKernels.hpp"

namespace Star {
namespace {

#ifdef __SWITCH2__
constexpr uint64_t FrameWaitTimeout = 30'000'000'000ULL;
#else
constexpr uint64_t FrameWaitTimeout = UINT64_MAX;
#endif

#ifdef __SWITCH2__
constexpr Uint32 VulkanWindowFlags = 0;
#else
constexpr Uint32 VulkanWindowFlags = SDL_WINDOW_VULKAN;
#endif

void checkVk(VkResult result, char const* operation) {
  if (result != VK_SUCCESS)
    throw RendererException::format("Vulkan {} failed (VkResult {})", operation, (int)result);
}

// Keep dispatch private to this backend. SDL loads the Vulkan driver only when
// the diagnostic window is created; the normal OpenGL path never loads it.
#define INSTANCE_FUNCTIONS(F) \
  F(vkDestroyInstance) F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) \
  F(vkGetPhysicalDeviceQueueFamilyProperties) F(vkGetPhysicalDeviceSurfaceSupportKHR) \
  F(vkEnumerateDeviceExtensionProperties) F(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  F(vkGetPhysicalDeviceSurfaceFormatsKHR) F(vkGetPhysicalDeviceSurfacePresentModesKHR) \
  F(vkCreateDevice) F(vkDestroySurfaceKHR) F(vkGetDeviceProcAddr) F(vkGetPhysicalDeviceMemoryProperties)
#define DEVICE_FUNCTIONS(F) \
  F(vkDestroyDevice) F(vkGetDeviceQueue) F(vkDeviceWaitIdle) \
  F(vkCreateSwapchainKHR) F(vkDestroySwapchainKHR) F(vkGetSwapchainImagesKHR) \
  F(vkCreateImageView) F(vkDestroyImageView) F(vkCreateRenderPass) F(vkDestroyRenderPass) \
  F(vkCreateFramebuffer) F(vkDestroyFramebuffer) F(vkCreateCommandPool) F(vkDestroyCommandPool) \
  F(vkAllocateCommandBuffers) F(vkResetCommandBuffer) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) \
  F(vkCmdBeginRenderPass) F(vkCmdEndRenderPass) F(vkCreateSemaphore) F(vkDestroySemaphore) \
  F(vkCreateFence) F(vkDestroyFence) F(vkWaitForFences) F(vkResetFences) \
  F(vkAcquireNextImageKHR) F(vkQueueSubmit) F(vkQueuePresentKHR) \
  F(vkCreateShaderModule) F(vkDestroyShaderModule) F(vkCreatePipelineLayout) F(vkDestroyPipelineLayout) \
  F(vkCreateGraphicsPipelines) F(vkDestroyPipeline) F(vkCmdBindPipeline) F(vkCmdSetViewport) \
  F(vkCmdSetScissor) F(vkCmdPushConstants) F(vkCmdBindVertexBuffers) F(vkCmdDraw) \
  F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements) F(vkAllocateMemory) \
  F(vkFreeMemory) F(vkBindBufferMemory) F(vkMapMemory) F(vkUnmapMemory) \
  F(vkFlushMappedMemoryRanges) F(vkInvalidateMappedMemoryRanges) F(vkCmdPipelineBarrier) F(vkCmdCopyImageToBuffer) \
  F(vkCreateImage) F(vkDestroyImage) F(vkGetImageMemoryRequirements) F(vkBindImageMemory) \
  F(vkCreateSampler) F(vkDestroySampler) F(vkCmdCopyBufferToImage) \
  F(vkCreateDescriptorSetLayout) F(vkDestroyDescriptorSetLayout) F(vkCreateDescriptorPool) \
  F(vkDestroyDescriptorPool) F(vkResetDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets) F(vkCmdBindDescriptorSets)

struct TextureFixtures {
  TexturePtr nearest, linear, wrap, alpha, rgb, bgr, bgra;
  std::vector<TexturePtr> solids;
};

struct WorldFixtures { TexturePtr emissive, translucent; };

WorldFixtures createWorldFixtures(Renderer& renderer) {
  renderer.loadConfig(JsonObject{{"frameBuffers", JsonObject{{"world", JsonObject{}}}}});
  renderer.loadEffectConfig("world", JsonObject{{"builtin", "world"}}, {});
  renderer.loadEffectConfig("interface", JsonObject{{"builtin", "interface"}}, {});
  return {renderer.createTexture(Image::filled({1, 1}, {200, 100, 50, 254})),
          renderer.createTexture(Image::filled({1, 1}, {200, 100, 50, 128}))};
}

Image diagnosticLightMap() {
  Image map(2, 2);
  map.set({0, 0}, Vec4B(255, 0, 0, 255)); map.set({1, 0}, Vec4B(0, 255, 0, 255));
  map.set({0, 1}, Vec4B(0, 0, 255, 255)); map.set({1, 1}, Vec4B(255, 255, 255, 255));
  return map;
}

void drawWorldScene(Renderer& renderer, WorldFixtures const& fixtures, uint64_t frame) {
  auto size = renderer.screenSize();
  float sx = size[0] / 960.0f, sy = size[1] / 540.0f;
  auto rect = [&](float x0, float y0, float x1, float y1) { return RectF({x0 * sx, y0 * sy}, {x1 * sx, y1 * sy}); };
  unsigned mapSize = frame % 3 == 2 ? 3 : 1;
  auto map = [&](Vec4B color) { renderer.setEffectTexture("lightMap", Image::filled({mapSize, mapSize}, color)); };
  auto tile = [&](Vec4B color) { renderer.setEffectTexture("tileLightMap", Image::filled({1, 1}, color)); };
  renderer.startFrame(); renderer.setScissorRect({});
  renderer.switchEffectConfig("interface");
  renderer.render(RenderQuad(rect(0, 0, 960, 540), {0, 255, 0, 255}));
  renderer.switchEffectConfig("world");
  if (frame % 7 == 6) {
    // No world vertices: composition must clear stale world contents. Exercise
    // changed, repeated, and restored scissor state after the clear-only pass.
    renderer.switchEffectConfig("interface");
    renderer.setScissorRect(RectI({(int)(40 * sx), (int)(40 * sy)}, {(int)(180 * sx), (int)(120 * sy)}));
    renderer.render(RenderQuad(rect(0, 0, 960, 540), {255, 0, 0, 255}));
    renderer.flush();
    renderer.render(RenderQuad(rect(0, 0, 960, 540), {0, 0, 255, 255}));
    renderer.setScissorRect({});
    renderer.render(RenderQuad(rect(210, 40, 350, 120), {255, 255, 0, 255}));
    renderer.finishFrame();
    return;
  }
  renderer.setEffectParameter("lightMapEnabled", true);
  renderer.setEffectParameter("lightMapMultiplier", 1.0f);
  renderer.setEffectParameter("lightMapScale", Vec2F(1, 1));
  renderer.setEffectParameter("lightMapOffset", Vec2F{});
  for (unsigned i = 1; i <= 6; ++i) renderer.setEffectParameter(strf("param{}", i), Vec3F{});
  tile({0, 0, 0, 0}); map(frame % 2 ? Vec4B(64, 128, 255, 255) : Vec4B(128, 64, 255, 255));
  renderer.render(RenderQuad(rect(0, 0, 960, 540), {20, 30, 40, 255}, 0));
  renderer.render(RenderQuad(rect(40, 340, 240, 500), {200, 160, 120, 255}, 1));
  map({64, 255, 128, 255});
  renderer.render(RenderQuad(rect(280, 340, 480, 500), {200, 160, 120, 255}, 1));
  renderer.setEffectParameter("lightMapMultiplier", 0.5f);
  renderer.render(RenderQuad(rect(520, 340, 720, 500), {200, 160, 120, 255}, 1));
  renderer.render(RenderQuad(rect(760, 340, 920, 500), {200, 160, 120, 255}, 0));
  renderer.setEffectParameter("lightMapMultiplier", 1.0f);
  tile({0, 64, 0, 128});
  renderer.render(RenderQuad(rect(40, 160, 240, 300), {200, 160, 120, 255}, 1));
  tile({0, 0, 0, 0}); map({0, 0, 0, 255});
  renderer.render(RenderQuad(fixtures.emissive, rect(280, 160, 480, 300), {255, 255, 255, 255}, 1));
  map({255, 255, 255, 255});
  renderer.setEffectParameter("param1", Vec3F(0.25f, 0, 0));
  renderer.render(RenderQuad(rect(520, 160, 720, 300), {80, 100, 120, 255}, 1));
  renderer.setEffectParameter("param1", Vec3F(0, 0, 1));
  renderer.render(RenderQuad(rect(760, 160, 920, 300), {200, 100, 50, 255}, 1));
  renderer.setEffectParameter("param1", Vec3F{});
  renderer.setEffectParameter("param2", Vec3F(0.5f, 1, 0.25f));
  renderer.setEffectParameter("param3", Vec3F(1, 0, 0));
  renderer.render(RenderQuad(rect(40, 40, 180, 120), {200, 160, 120, 255}, 1));
  renderer.setEffectParameter("lightMapEnabled", false);
  renderer.render(RenderQuad(rect(210, 40, 350, 120), {200, 160, 120, 255}, 1));
  renderer.setEffectParameter("lightMapEnabled", true);
  renderer.setEffectParameter("param3", Vec3F{});
  renderer.render(RenderQuad(fixtures.translucent, rect(380, 40, 520, 120), {255, 255, 255, 255}, 1));
  renderer.setEffectTexture("lightMap", diagnosticLightMap());
  renderer.setEffectParameter("lightMapScale", Vec2F(size[0] / 2.0f, size[1] / 2.0f));
  renderer.render(RenderQuad(rect(550, 40, 690, 120), {255, 255, 255, 255}, 1));
  renderer.setEffectParameter("lightMapOffset", Vec2F(size[0] / 4.0f, size[1] / 4.0f));
  renderer.render(RenderQuad(rect(720, 40, 860, 120), {255, 255, 255, 255}, 1));
  renderer.setEffectTexture("lightMap", Image());
  renderer.setEffectTexture("tileLightMap", Image());
  renderer.render(RenderQuad(rect(900, 50, 920, 120), {70, 100, 140, 255}, 1));
  renderer.switchEffectConfig("interface");
  renderer.render(RenderQuad(rect(120, 390, 200, 450), {255, 255, 255, 128}));
  // The framebuffer is copied once per frame, like the OpenGL blitted guard.
  // A later world pass must not wipe out the UI or compose a second time.
  renderer.switchEffectConfig("world");
  renderer.setEffectParameter("lightMapEnabled", false);
  renderer.render(RenderQuad(rect(900, 10, 920, 30), {0, 0, 255, 255}));
  renderer.switchEffectConfig("interface");
  renderer.finishFrame();
}

struct AtlasFixtures {
  TextureGroupPtr nearestGroup, linearGroup;
  TexturePtr nearest, linear, oversized;
  std::vector<TexturePtr> tiles;
  RenderBufferPtr cached;
};

Vec4B atlasTileColor(unsigned index) {
  return Vec4B(30 + index % 6 * 30, 80 + index % 5 * 20, 210 - index % 7 * 20, 255);
}

AtlasFixtures createAtlasFixtures(Renderer& renderer) {
  AtlasFixtures atlas;
  atlas.nearestGroup = renderer.createTextureGroup(TextureGroupSize::Small);
  atlas.linearGroup = renderer.createTextureGroup(TextureGroupSize::Small, TextureFiltering::Linear);
  Image corners(2, 2);
  corners.set({0, 0}, Vec4B(255, 0, 0, 255));
  corners.set({1, 0}, Vec4B(0, 255, 0, 255));
  corners.set({0, 1}, Vec4B(0, 0, 255, 255));
  corners.set({1, 1}, Vec4B(255, 255, 0, 255));
  atlas.nearest = atlas.nearestGroup->create(corners);
  atlas.linear = atlas.linearGroup->create(corners);
  // 270 one-cell images force at least two 256x256 pages, exercising page and
  // cell offsets rather than just the first entry of an otherwise empty page.
  for (unsigned i = 0; i < 270; ++i)
    atlas.tiles.push_back(atlas.nearestGroup->create(Image::filled({3, 3}, atlasTileColor(i))));
  atlas.oversized = atlas.nearestGroup->create(Image::filled({300, 3}, {240, 150, 30, 255}));
  atlas.cached = renderer.createRenderBuffer();
  List<RenderPrimitive> mesh{RenderQuad(atlas.nearest, RectF({0, 0}, {200, 140}))};
  atlas.cached->set(mesh);
  return atlas;
}

Vec4B atlasOldColor(uint64_t frame) { return {uint8_t(40 + frame * 17 % 180), 20, 160, 255}; }
Vec4B atlasNewColor(uint64_t frame) { return {10, uint8_t(60 + frame * 13 % 180), 60, 255}; }

void drawAtlasScene(Renderer& renderer, AtlasFixtures const& atlas, uint64_t frame) {
  auto size = renderer.screenSize();
  float sx = size[0] / 960.0f, sy = size[1] / 540.0f;
  auto rect = [&](float x0, float y0, float x1, float y1) { return RectF({x0 * sx, y0 * sy}, {x1 * sx, y1 * sy}); };
  renderer.startFrame();
  renderer.setScissorRect({});
  renderer.render(RenderQuad(atlas.nearest, rect(40, 340, 240, 500)));
  renderer.render(RenderQuad(atlas.linear, rect(280, 340, 480, 500)));
  renderer.render(RenderQuad(atlas.tiles.back(), rect(520, 340, 720, 500)));
  renderer.render(RenderQuad(atlas.oversized, rect(760, 340, 920, 500)));
  renderer.flush();
  // Free and reuse an atlas region after a draw was queued. The old red draw
  // must retain its page version. Change colors each frame to also detect stale
  // tile uploads when GPU allocations are reused across frames.
  auto first = atlas.nearestGroup->create(Image::filled({3, 3}, atlasOldColor(frame)));
  renderer.render(RenderQuad(first, rect(40, 160, 240, 300)));
  renderer.flush();
  first.reset();
  auto second = atlas.nearestGroup->create(Image::filled({3, 3}, atlasNewColor(frame)));
  renderer.render(RenderQuad(second, rect(280, 160, 480, 300)));
  renderer.flush();
  second.reset();
  renderer.renderBuffer(atlas.cached, Mat3F::translation({520 * sx, 160 * sy}) * Mat3F::scaling({sx, sy}));
  auto temporaryGroup = renderer.createTextureGroup(TextureGroupSize::Small);
  auto orphan = temporaryGroup->create(Image::filled({3, 3}, {190, 60, 220, 255}));
  renderer.render(RenderQuad(orphan, rect(760, 160, 920, 300)));
  renderer.flush();
  orphan.reset();
  temporaryGroup.reset();
  unsigned indices[]{0, 1, 63, 128, 200, 254, 255, 269};
  for (unsigned i = 0; i < 8; ++i)
    renderer.render(RenderQuad(atlas.tiles[indices[i]], rect(40 + i * 110, 40, 135 + i * 110, 120)));
  renderer.finishFrame();
}

TextureFixtures createTextureFixtures(Renderer& renderer) {
  TextureFixtures textures;
  Image corners(2, 2);
  corners.set({0, 0}, Vec4B(255, 0, 0, 255));
  corners.set({1, 0}, Vec4B(0, 255, 0, 255));
  corners.set({0, 1}, Vec4B(0, 0, 255, 255));
  corners.set({1, 1}, Vec4B(255, 255, 0, 255));
  textures.nearest = renderer.createTexture(corners);
  textures.linear = renderer.createTexture(corners, TextureAddressing::Clamp, TextureFiltering::Linear);
  textures.wrap = renderer.createTexture(corners, TextureAddressing::Wrap);
  Image alpha(2, 2);
  alpha.set({0, 0}, Vec4B(255, 255, 255, 0));
  alpha.set({1, 0}, Vec4B(255, 255, 255, 128));
  alpha.set({0, 1}, Vec4B(255, 255, 255, 255));
  alpha.set({1, 1}, Vec4B(255, 255, 255, 64));
  textures.alpha = renderer.createTexture(alpha);
  textures.rgb = renderer.createTexture(Image::filled({3, 1}, {20, 80, 200, 255}).convert(PixelFormat::RGB24));
  textures.bgr = renderer.createTexture(Image::filled({3, 1}, {200, 80, 20, 255}).convert(PixelFormat::BGR24));
  textures.bgra = renderer.createTexture(Image::filled({3, 1}, {40, 220, 80, 128}).convert(PixelFormat::BGRA32));
  for (auto color : {Vec4B(255, 0, 0, 255), Vec4B(0, 255, 0, 255), Vec4B(0, 0, 255, 255),
                     Vec4B(255, 255, 0, 255), Vec4B(255, 0, 255, 255), Vec4B(0, 255, 255, 255)})
    textures.solids.push_back(renderer.createTexture(Image::filled({1, 1}, color)));
  return textures;
}

void drawTextureScene(Renderer& renderer, TextureFixtures const& textures, bool singleTexture) {
  auto size = renderer.screenSize();
  float sx = size[0] / 960.0f, sy = size[1] / 540.0f;
  auto rect = [&](float x0, float y0, float x1, float y1) { return RectF({x0 * sx, y0 * sy}, {x1 * sx, y1 * sy}); };
  auto extended = [&](TexturePtr texture, RectF const& screen) {
    renderer.render(RenderQuad(texture, screen.min(), {-2, -2}, {screen.xMax(), screen.yMin()}, {6, -2},
        screen.max(), {6, 6}, {screen.xMin(), screen.yMax()}, {-2, 6}));
  };
  renderer.startFrame();
  renderer.setScissorRect({});
  renderer.setMultiTexturingEnabled(!singleTexture);
  renderer.render(RenderQuad(textures.nearest, rect(40, 340, 240, 500)));
  renderer.render(RenderQuad(textures.linear, rect(280, 340, 480, 500)));
  extended(textures.wrap, rect(520, 340, 720, 500));
  extended(textures.nearest, rect(760, 340, 920, 500));
  renderer.render(RenderQuad(rect(40, 160, 240, 300), {80, 80, 80, 255}));
  renderer.render(RenderQuad(textures.alpha, rect(40, 160, 240, 300)));
  renderer.render(RenderQuad(textures.nearest, rect(280, 160, 480, 300), {128, 255, 128, 255}));
  renderer.render(RenderQuad(textures.rgb, rect(520, 160, 600, 300)));
  renderer.render(RenderQuad(textures.bgr, rect(630, 160, 710, 300)));
  renderer.render(RenderQuad(textures.bgra, rect(740, 160, 820, 300)));

  // New immutable texture every frame, released before upload/command recording.
  // Mutating the caller's image must not mutate the texture's copied source.
  Image source = Image::filled({3, 5}, {245, 210, 40, 255});
  auto transient = renderer.createTexture(source);
  source.fill(Vec4B(255, 0, 0, 255));
  renderer.render(RenderQuad(transient, rect(850, 160, 920, 300)));
  renderer.flush();
  transient.reset();

  auto cached = renderer.createRenderBuffer();
  List<RenderPrimitive> mesh{RenderQuad(textures.solids[0], RectF({-20, -8}, {20, 8}))};
  cached->set(mesh);
  renderer.renderBuffer(cached, Mat3F::translation({110 * sx, 320 * sy}) * Mat3F::scaling({sx, sy}));
  mesh = {RenderQuad(textures.solids[1], RectF({-20, -8}, {20, 8}))};
  cached->set(mesh);
  renderer.renderBuffer(cached, Mat3F::translation({220 * sx, 320 * sy}) * Mat3F::scaling({sx, sy}));
  cached.reset();

  // Twelve quads force multiple batches, exercise all four slots, and revisit
  // textures across boundaries. No sorting may change their draw order.
  renderer.flush();
  for (unsigned i = 0; i < 12; ++i)
    renderer.render(RenderQuad(textures.solids[i % 6], rect(40 + i * 74, 40, 100 + i * 74, 120)));
  renderer.flush();
  renderer.setScissorRect(RectI({(int)(400 * sx), (int)(8 * sy)}, {(int)(560 * sx), (int)(28 * sy)}));
  renderer.render(RenderQuad(textures.solids[4], rect(350, 0, 610, 35)));
  renderer.setScissorRect({});
  renderer.finishFrame();
}

// Exercise the same API used by the game. Coordinates are authored in a 960x540
// reference space so the test scene remains legible at different window sizes.
void drawGeometryScene(Renderer& renderer) {
  auto size = renderer.screenSize();
  float sx = size[0] / 960.0f, sy = size[1] / 540.0f;
  auto rect = [&](float x0, float y0, float x1, float y1) { return RectF({x0 * sx, y0 * sy}, {x1 * sx, y1 * sy}); };
  renderer.startFrame();
  renderer.setScissorRect({});
  renderer.render(RenderQuad(rect(60, 70, 280, 250), {230, 70, 60, 255}));
  renderer.flush();
  renderer.render(RenderQuad(rect(180, 150, 380, 330), {40, 130, 240, 128}));
  renderer.render(RenderTriangle({60 * sx, 370 * sy}, {280 * sx, 370 * sy}, {170 * sx, 490 * sy}, {70, 220, 140, 255}));

  auto cached = renderer.createRenderBuffer();
  List<RenderPrimitive> mesh{RenderQuad(RectF({-60, -60}, {60, 60}), {245, 200, 55, 255})};
  cached->set(mesh);
  renderer.renderBuffer(cached, Mat3F::translation({490 * sx, 210 * sy}) * Mat3F::scaling({sx, sy}) * Mat3F::rotation(0.4f));
  // Mutation after queuing the yellow square must not recolor that draw.
  mesh = {RenderQuad(RectF({-60, -60}, {60, 60}), {215, 80, 210, 255})};
  cached->set(mesh);
  renderer.renderBuffer(cached, Mat3F::translation({510 * sx, 410 * sy}) * Mat3F::scaling({sx * 0.6f, sy * 0.6f}));
  renderer.render(RenderPoly({{680 * sx, 410 * sy}, {750 * sx, 350 * sy}, {820 * sx, 410 * sy}, {750 * sx, 470 * sy}}, {60, 210, 220, 255}));

  renderer.render(RenderQuad(rect(656, 106, 884, 294), {180, 190, 205, 255}));
  renderer.render(RenderQuad(rect(660, 110, 880, 290), {6, 26, 64, 255}));
  renderer.setScissorRect(RectI({(int)(660 * sx), (int)(110 * sy)}, {(int)(880 * sx), (int)(290 * sy)}));
  renderer.render(RenderQuad(rect(610, 150, 930, 260), {80, 220, 130, 255}));
  renderer.setScissorRect({});
  renderer.render(RenderQuad(rect(895, 310, 915, 330), {255, 255, 255, 255}));

  // Empty and offscreen clips must draw nothing. Negative and oversized clips
  // must be clamped safely before conversion to Vulkan's unsigned extents.
  renderer.setScissorRect(RectI({-100, -100}, {-10, -10}));
  renderer.render(RenderQuad(rect(0, 0, 960, 540), {255, 255, 255, 255}));
  renderer.setScissorRect(RectI({0, 0}, {0, 0}));
  renderer.render(RenderQuad(rect(0, 0, 960, 540), {255, 255, 255, 255}));
  renderer.setScissorRect(RectI({-50, -50}, {(int)size[0] + 50, (int)size[1] + 50}));
  renderer.render(RenderQuad(rect(10, 10, 30, 30), {255, 160, 40, 255}));
  renderer.setScissorRect({});
  renderer.finishFrame();
}

class SdlVulkanDiagnostic : public SdlRenderBackend {
public:
  explicit SdlVulkanDiagnostic(bool diagnostic = true, bool gameplay = false) : m_diagnostic(diagnostic), m_gameplay(gameplay) {}
  ~SdlVulkanDiagnostic() override {
    shutdown();
  }

  void shutdown() {
    if (m_device) {
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "device idle during shutdown");
      vkDeviceWaitIdle(m_device);
      if (m_benchmarkQueries && m_destroyBenchmarkQueries) m_destroyBenchmarkQueries(m_device,m_benchmarkQueries,nullptr);
      m_benchmarkQueries=VK_NULL_HANDLE;
      if (m_diagnostic && m_worldScene)
        Logger::info("Vulkan lighting allocations: {}, reuses: {} (no idle lighting pool)", m_lightingAllocations, m_lightingReuses);
      m_renderer.reset();
      m_textureFixtures = {};
      m_atlasFixtures = {};
      m_worldFixtures = {};
      if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
      m_descriptorPool = VK_NULL_HANDLE;
      for (auto& entry : m_textures) destroyTexture(entry.second);
      m_textures.clear();
      destroyHostBuffer(m_textureStaging);
      destroyHostBuffer(m_vertices);
      destroyHostBuffer(m_readback);
      destroyHostBuffer(m_effectUniforms);
      destroySwapchain();
      if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
      m_layout = VK_NULL_HANDLE;
      if (m_textureLayout) vkDestroyDescriptorSetLayout(m_device, m_textureLayout, nullptr);
      m_textureLayout = VK_NULL_HANDLE;
      if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
      if (m_acquired) vkDestroySemaphore(m_device, m_acquired, nullptr);
      if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
      vkDestroyDevice(m_device, nullptr);
      m_device = VK_NULL_HANDLE;
    }
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
    if (m_debugMessenger && m_destroyDebug)
      m_destroyDebug(m_instance, m_debugMessenger, nullptr);
    m_debugMessenger = VK_NULL_HANDLE;
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }

  Uint32 windowFlags() const override {
    // SDL supplies input and window events; OunceVK owns presentation.
    return VulkanWindowFlags;
  }

  RendererPtr createRenderer() override {
    if (!m_renderer) {
      m_renderer = make_shared<VulkanRenderer>(m_gameplay);
      if (m_diagnostic) {
        if (m_worldScene) m_worldFixtures = createWorldFixtures(*m_renderer);
        else if (m_atlasScene) m_atlasFixtures = createAtlasFixtures(*m_renderer);
        else m_textureFixtures = createTextureFixtures(*m_renderer);
      }
    }
    return m_renderer;
  }

  void initialize(SDL_Window* window) override {
    m_window = window;
#ifdef __SWITCH2__
    m_getInstanceProc = &::vkGetInstanceProcAddr;
#else
    m_getInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!m_getInstanceProc)
      throw RendererException::format("SDL could not load Vulkan: {}", SDL_GetError());
#endif
    auto createInstance = global<PFN_vkCreateInstance>("vkCreateInstance");
    auto enumerateLayers = global<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");
    auto enumerateExtensions = global<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");

#ifdef __SWITCH2__
    std::vector<char const*> extensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME};
#else
    unsigned extensionCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr))
      throw RendererException::format("SDL Vulkan extensions: {}", SDL_GetError());
    std::vector<char const*> extensions(extensionCount);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data()))
      throw RendererException::format("SDL Vulkan extensions: {}", SDL_GetError());
#endif

    uint32_t count = 0;
    checkVk(enumerateLayers(&count, nullptr), "enumerate layers");
    std::vector<VkLayerProperties> layers(count);
    checkVk(enumerateLayers(&count, layers.data()), "enumerate layers");
    char const* validationLayer = "VK_LAYER_KHRONOS_validation";
    bool validation = std::any_of(layers.begin(), layers.end(), [&](auto const& layer) {
      return std::strcmp(layer.layerName, validationLayer) == 0;
    });
    const char* validationMode = SDL_getenv("XSB_VULKAN_VALIDATION");
    bool validationDisabled = validationMode && (std::strcmp(validationMode, "off") == 0 || std::strcmp(validationMode, "0") == 0);
    bool validationRequired = validationMode && (std::strcmp(validationMode, "on") == 0 || std::strcmp(validationMode, "1") == 0);
    if (validationMode && !validationDisabled && !validationRequired && std::strcmp(validationMode, "auto") != 0)
      throw RendererException("XSB_VULKAN_VALIDATION must be auto, on, off, 1, or 0");
    if (validationRequired && !validation)
      throw RendererException("XSB_VULKAN_VALIDATION=on requires VK_LAYER_KHRONOS_validation");
    if (validationDisabled) validation = false;
    checkVk(enumerateExtensions(nullptr, &count, nullptr), "enumerate extensions");
    std::vector<VkExtensionProperties> available(count);
    checkVk(enumerateExtensions(nullptr, &count, available.data()), "enumerate extensions");
    auto hasExtension = [&](char const* name) {
      return std::any_of(available.begin(), available.end(), [&](auto const& ext) {
        return std::strcmp(ext.extensionName, name) == 0;
      });
    };
    bool debug = validation && hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debug) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    bool portability = hasExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (portability) extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "xStarbound Vulkan texture diagnostic";
    app.apiVersion = VK_API_VERSION_1_0;
    VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugMessage;
    debugInfo.pUserData = this;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = (uint32_t)extensions.size();
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    if (portability) instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    if (validation) {
      instanceInfo.enabledLayerCount = 1;
      instanceInfo.ppEnabledLayerNames = &validationLayer;
    }
    if (debug) instanceInfo.pNext = &debugInfo;
    checkVk(createInstance(&instanceInfo, nullptr, &m_instance), "create instance");
#define LOAD_INSTANCE(name) name = instance<PFN_##name>(#name);
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    if (debug) {
      auto createDebug = instance<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT");
      m_destroyDebug = instance<PFN_vkDestroyDebugUtilsMessengerEXT>("vkDestroyDebugUtilsMessengerEXT");
      checkVk(createDebug(m_instance, &debugInfo, nullptr, &m_debugMessenger), "create debug messenger");
    }
    Logger::info("Vulkan: validation {}, debug messages {}", validation ? "enabled" : validationDisabled ? "disabled" : "unavailable", debug ? "enabled" : "unavailable");
#ifdef __SWITCH2__
    VkViSurfaceCreateInfoNN surfaceInfo{VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN};
    surfaceInfo.window = nwindowGetDefault();
    auto createSurface = instance<PFN_vkCreateViSurfaceNN>("vkCreateViSurfaceNN");
    checkVk(createSurface(m_instance, &surfaceInfo, nullptr, &m_surface), "create VI surface");
#else
    if (!SDL_Vulkan_CreateSurface(window, m_instance, &m_surface))
      throw RendererException::format("SDL Vulkan surface: {}", SDL_GetError());
#endif
    chooseDevice();

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (auto family : {m_queueFamily, m_presentFamily}) {
      if (!queues.empty() && family == queues.front().queueFamilyIndex) continue;
      VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
      queueInfo.queueFamilyIndex = family;
      queueInfo.queueCount = 1;
      queueInfo.pQueuePriorities = &priority;
      queues.push_back(queueInfo);
    }
    std::vector<char const*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // MoltenVK and other portability implementations may require this extension.
    if (deviceHasExtension(m_physical, "VK_KHR_portability_subset"))
      deviceExtensions.push_back("VK_KHR_portability_subset");
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = (uint32_t)queues.size();
    deviceInfo.pQueueCreateInfos = queues.data();
    deviceInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    checkVk(vkCreateDevice(m_physical, &deviceInfo, nullptr, &m_device), "create device");
#define LOAD_DEVICE(name) name = device<PFN_##name>(#name);
    DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = m_queueFamily;
    checkVk(vkCreateCommandPool(m_device, &pool, nullptr, &m_pool), "create command pool");
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = m_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(m_device, &allocation, &m_commands), "allocate command buffer");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    checkVk(vkCreateSemaphore(m_device, &semaphore, nullptr, &m_acquired), "create acquire semaphore");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateFence(m_device, &fence, nullptr, &m_fence), "create frame fence");
  }

  void setVSyncEnabled(bool enabled) override {
    if (m_vsync != enabled) {
      m_vsync = enabled;
      m_dirty = true;
      Logger::info("Vulkan: VSync {}", enabled ? "enabled" : "disabled");
    }
  }

  void resize() { m_dirty = true; }
  unsigned validationErrors() const { return m_validationErrors.load(); }
  uint64_t framesPresented() const { return m_frames; }
  uint64_t verifiedFrames() const { return m_verifiedFrames; }
  uint64_t uploadedTextures() const { return m_uploadedTextures; }
  uint64_t retiredTextures() const { return m_retiredTextures; }
  uint64_t uniqueDescriptors() const { return m_uniqueDescriptors; }
  uint64_t reusedDescriptors() const { return m_reusedDescriptors; }
  void enableVerification() { m_verify = true; }
  void useGeometryScene() { m_textureScene = false; }
  void useSingleTextureBatches() { m_singleTexture = true; }
  void useAtlasScene() { m_atlasScene = true; }
  void useWorldScene() { m_worldScene = true; }

  void present() override {
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "prepare frame");
    if (!m_diagnostic && !m_renderer) return;
    int width = 0, height = 0;
#ifdef __SWITCH2__
    u32 nativeWidth = 0, nativeHeight = 0;
    Result dimensionsResult = nwindowGetDimensions(nwindowGetDefault(), &nativeWidth, &nativeHeight);
    if (R_FAILED(dimensionsResult))
      throw RendererException::format("Could not query VI window dimensions: {}", dimensionsResult);
    width = static_cast<int>(nativeWidth);
    height = static_cast<int>(nativeHeight);
#else
    SDL_Vulkan_GetDrawableSize(m_window, &width, &height);
#endif
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED))
      return;
    // Normal application mode does not run the diagnostic event loop's resize
    // hook. Detect drawable changes here as well as OUT_OF_DATE/SUBOPTIMAL.
    if (m_lastDrawableSize != Vec2U(width, height)) {
      m_lastDrawableSize = Vec2U(width, height);
      m_dirty = true;
    }
    if (m_renderer && m_renderer->worldEnabled() && !m_worldTarget.image) m_dirty = true;
    if (m_dirty || !m_swapchain) {
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "recreate swapchain");
      recreateSwapchain(width, height);
    }
    if (!m_swapchain) return;
    {
      STAR_RENDER_ZONE("Vulkan wait frame fence");
      Switch2FrameProfile::Zone fenceZone(Switch2FrameProfile::Phase::Fence);
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "wait frame fence (30s console timeout)");
      Switch2WaitProfile::Scope fenceWaits(Switch2WaitProfile::Metric::Fence);
      checkVk(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, FrameWaitTimeout), "wait frame fence");
    }
    if (!m_renderer) createRenderer();
    m_renderer->setScreenSize({m_extent.width, m_extent.height});
    if (m_diagnostic) {
      if (m_worldScene) drawWorldScene(*m_renderer, m_worldFixtures, m_frames);
      else if (m_atlasScene) drawAtlasScene(*m_renderer, m_atlasFixtures, m_frames);
      else if (m_textureScene) drawTextureScene(*m_renderer, m_textureFixtures, m_singleTexture);
      else drawGeometryScene(*m_renderer);
    }
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "upload geometry/textures");
    uploadGeometry();
    uint32_t imageIndex = 0;
    VkResult acquired;
    {
      STAR_RENDER_ZONE("Vulkan acquire swapchain image");
      Switch2FrameProfile::Zone acquireZone(Switch2FrameProfile::Phase::Acquire);
      Switch2WaitProfile::Scope acquireWaits(Switch2WaitProfile::Metric::Acquire);
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "acquire swapchain image (30s console timeout)");
      acquired = vkAcquireNextImageKHR(m_device, m_swapchain, FrameWaitTimeout, m_acquired, VK_NULL_HANDLE, &imageIndex);
    }
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      m_dirty = true;
      return;
    }
    if (acquired != VK_SUBOPTIMAL_KHR) checkVk(acquired, "acquire image");
    checkVk(vkResetCommandBuffer(m_commands, 0), "reset commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(m_commands, &begin), "begin commands");
    prepareTextures();
    VkClearValue clear{};
    clear.color.float32[0] = m_diagnostic ? 0.025f : 0.0f;
    clear.color.float32[1] = m_diagnostic ? 0.10f : 0.0f;
    clear.color.float32[2] = m_diagnostic ? 0.25f : 0.0f;
    clear.color.float32[3] = 1.0f;
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "record world geometry");
    recordGeometry(imageIndex, clear);
    bool verify = m_verify && (m_verifyNext || m_atlasScene || m_worldScene);
    if (verify) recordReadback(imageIndex);
    checkVk(vkEndCommandBuffer(m_commands), "end commands");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commands;
    submit.signalSemaphoreCount = 1;
    // Present semaphores belong to swapchain images, not CPU frame slots.
    submit.pSignalSemaphores = &m_finished[imageIndex];
    checkVk(vkResetFences(m_device, 1, &m_fence), "reset frame fence");
    {
      STAR_RENDER_ZONE("Vulkan queue submit");
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "queue submit");
      checkVk(vkQueueSubmit(m_queue, 1, &submit, m_fence), "submit");
    }
    if (verify) {
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "wait readback fence (30s console timeout)");
      checkVk(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, FrameWaitTimeout), "wait geometry readback");
      verifyGeometry();
      m_verifyNext = false;
    }
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_finished[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult result;
    {
      STAR_RENDER_ZONE("Vulkan queue present");
      Switch2FrameProfile::Zone presentZone(Switch2FrameProfile::Phase::Present);
      Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "queue present");
      result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR)
      m_dirty = true;
    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
      checkVk(result, "present");
    Switch2FrameProfile::count(Switch2FrameProfile::Counter::Draws, m_renderer->draws().size());
    Switch2FrameProfile::count(Switch2FrameProfile::Counter::Vertices, m_renderer->vertices().size());
    STAR_RENDER_PLOT("Vulkan draw batches/frame", m_renderer->draws().size());
    STAR_RENDER_PLOT("Vulkan vertices/frame", m_renderer->vertices().size());
    STAR_RENDER_COUNTER("Vulkan atlas snapshots/frame", atlasSnapshots);
    STAR_RENDER_COUNTER("Vulkan atlas CPU copy bytes/frame", atlasSnapshotBytes);
    STAR_RENDER_COUNTER("Vulkan atlas region bytes/frame", atlasWriteBytes);
    STAR_RENDER_COUNTER("Asset texture cache misses/frame", textureCacheMisses);
    STAR_RENDER_COUNTER("Vulkan retired textures/frame", retiredTextures);
    STAR_RENDER_COUNTER("Vulkan batch texture-limit splits/frame", batchTextureLimitSplits);
    STAR_RENDER_COUNTER("Vulkan immediate batches/frame", immediateBatches);
    STAR_RENDER_COUNTER("Vulkan cached replay batches/frame", cachedReplayBatches);
    STAR_RENDER_COUNTER("Vulkan cached prepared batches/frame", cachedPreparedBatches);
    STAR_RENDER_COUNTER("Vulkan merged adjacent draws/frame", mergedDraws);
    STAR_RENDER_COUNTER("Vulkan merge range breaks/frame", mergeRangeBreaks);
    STAR_RENDER_COUNTER("Vulkan merge state breaks/frame", mergeStateBreaks);
    STAR_RENDER_COUNTER("Vulkan merge texture breaks/frame", mergeTextureBreaks);
    ++m_frames;
    Switch2Diagnostics::count(Switch2Diagnostics::Counter::Frames);
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "frame presented");
  }

#include "StarVulkanBenchmark.inl"

private:
  struct HostBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
    bool coherent = false;
    unsigned underusedFrames = 0;
  };
  struct PushState {
    float rows[3][4];
    float screen[4];
  };
  static_assert(sizeof(PushState) == 64, "Push constants must match flat.vert");
  struct WorldUniform {
    float mapTransform[4];
    float lighting[4];
    float parameters[6][4];
    float screen[4];
  };
  static_assert(sizeof(WorldUniform) == 144, "World uniform must match std140 shader offsets");
  struct GpuTexture {
    std::weak_ptr<VulkanTextureData const> source;
    std::weak_ptr<uint8_t const> atlasOwner;
    std::vector<shared_ptr<Image const>> atlasTiles;
    Vec2U size{};
    bool initialized = false;
    bool lighting = false;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
  };

  struct UploadRegion { Image const* pixels; Vec2U offset; VkDeviceSize bufferOffset; };
  struct Upload { VulkanTextureData const* data; bool initialized; size_t firstRegion; size_t regionCount; };

  // Scratch arrays retain at most 1 MiB each between frames. In particular,
  // these arrays never keep immutable texture sources alive between frames.
  template <typename T>
  static void clearScratch(std::vector<T>& values) {
    values.clear();
    if (values.capacity() > 1024 * 1024 / sizeof(T)) std::vector<T>().swap(values);
  }

  void destroyHostBuffer(HostBuffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(m_device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
  }

  void ensureHostBuffer(HostBuffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage) {
    // Cap speculative headroom at 1 MiB, and reclaim exceptional high-water
    // marks after 120 consecutive underused frames. Called once per frame,
    // including zero-byte frames, only after the frame fence has completed.
    constexpr VkDeviceSize headroom = 1024 * 1024;
    bool underused = bytes <= buffer.capacity / 2 && buffer.capacity - bytes > headroom;
    buffer.underusedFrames = underused ? buffer.underusedFrames + 1 : 0;
    bool shrink = buffer.underusedFrames >= 120;
    if (buffer.capacity >= bytes && !shrink) return;
    VkDeviceSize capacity = std::max<VkDeviceSize>(bytes, 4096);
    if (!shrink)
      capacity = std::max(capacity, buffer.capacity + std::min(buffer.capacity / 2, headroom));
    STAR_RENDER_ZONE("Vulkan host buffer reallocation");
    STAR_RENDER_TEXT("%llu bytes", (unsigned long long)bytes);
    destroyHostBuffer(buffer);
    // The caller has waited for this frame slot's fence before resizing or
    // writing. No buffer storage referenced by an earlier submission is reused.
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = capacity;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(m_device, &info, nullptr, &buffer.buffer), "create host buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physical, &properties);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
      auto flags = properties.memoryTypes[i].propertyFlags;
      if ((requirements.memoryTypeBits & (1u << i)) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        memoryType = i;
        if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
      }
    }
    if (memoryType == UINT32_MAX) throw RendererException("No host-visible Vulkan buffer memory type");
    buffer.coherent = (properties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    checkVk(vkAllocateMemory(m_device, &allocation, nullptr, &buffer.memory), "allocate host buffer");
    checkVk(vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0), "bind host buffer");
    checkVk(vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "map host buffer");
    buffer.capacity = info.size;
  }

  void uploadGeometry() {
    Switch2FrameProfile::Zone geometryZone(Switch2FrameProfile::Phase::GeometryUpload);
    STAR_RENDER_ZONE("Vulkan vertex upload CPU");
    auto const& vertices = m_renderer->vertices();
    VkDeviceSize bytes = vertices.size() * sizeof(VulkanRenderer::Vertex);
    ensureHostBuffer(m_vertices, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (vertices.empty()) return;
    std::memcpy(m_vertices.mapped, vertices.data(), (size_t)bytes);
    if (!m_vertices.coherent) {
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = m_vertices.memory;
      range.size = VK_WHOLE_SIZE;
      checkVk(vkFlushMappedMemoryRanges(m_device, 1, &range), "flush vertices");
    }
  }

  void destroyTexture(GpuTexture& texture) {
    STAR_RENDER_ZONE("Vulkan GPU texture destruction");
    if (texture.sampler) vkDestroySampler(m_device, texture.sampler, nullptr);
    if (texture.view) vkDestroyImageView(m_device, texture.view, nullptr);
    if (texture.image) vkDestroyImage(m_device, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(m_device, texture.memory, nullptr);
    texture = {};
  }

  void createTexture(GpuTexture& texture, VulkanTextureDataPtr const& source, Vec2U targetSize = {}) {
    STAR_EXPLORATION_SCOPE(trace,Switch2ExplorationProfile::Kind::GpuTexture,targetSize!=Vec2U{}?"render-target":"sampled-texture");
    Switch2FrameProfile::count(Switch2FrameProfile::Counter::NewImages, 1);
    STAR_RENDER_ZONE("Vulkan GPU texture creation");
    STAR_RENDER_TEXT("%ux%u", targetSize == Vec2U{} ? source->size()[0] : targetSize[0], targetSize == Vec2U{} ? source->size()[1] : targetSize[1]);
    texture.source = source;
    bool target = targetSize != Vec2U{};
    auto size = target ? targetSize : source->size();
    texture.size = size;
    texture.atlasOwner = source->atlasOwner;
    texture.lighting = source->lighting;
    VkPhysicalDeviceProperties limits{};
    vkGetPhysicalDeviceProperties(m_physical, &limits);
    if (size[0] > limits.limits.maxImageDimension2D || size[1] > limits.limits.maxImageDimension2D)
      throw RendererException("Vulkan texture exceeds device image size limit");
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = target ? m_format : VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {size[0], size[1], 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = (target ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT) | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateImage(m_device, &image, nullptr, &texture.image), "create sampled image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, texture.image, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physical, &properties);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
      if (requirements.memoryTypeBits & (1u << i)) {
        memoryType = i;
        if (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
      }
    }
    if (memoryType == UINT32_MAX) throw RendererException("No Vulkan sampled-image memory type");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    checkVk(vkAllocateMemory(m_device, &allocation, nullptr, &texture.memory), "allocate texture memory");
    checkVk(vkBindImageMemory(m_device, texture.image, texture.memory, 0), "bind sampled image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = texture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    checkVk(vkCreateImageView(m_device, &view, nullptr, &texture.view), "create sampled-image view");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.minFilter = sampler.magFilter = source->filtering == TextureFiltering::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = source->addressing == TextureAddressing::Wrap ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxAnisotropy = 1;
    sampler.minLod = sampler.maxLod = 0;
    checkVk(vkCreateSampler(m_device, &sampler, nullptr, &texture.sampler), "create texture sampler");
  }

  void prepareTextures() {
    Switch2FrameProfile::Zone texturesZone(Switch2FrameProfile::Phase::Textures);
    STAR_RENDER_ZONE("Vulkan prepare textures and descriptors");
    // present() waited for the frame fence. No previous descriptor or resource
    // can still be referenced by GPU work in this single-frame implementation.
    size_t count = m_renderer->draws().size();
    if (count > UINT32_MAX / 6) throw RendererException("Too many Vulkan texture batches");
    // Protect every version used by this frame before considering reuse.
    std::map<uint64_t, VulkanTextureDataPtr> required;
    auto require = [&](VulkanTextureDataPtr const& data) { required.emplace(data->id, data); };
    require(m_renderer->whiteTexture());
    for (auto const& draw : m_renderer->draws()) {
      for (auto const& data : draw.textures) require(data);
      if (draw.world) { require(draw.effect.lightMap); require(draw.effect.tileLightMap); }
    }
    auto retire = [&](auto it) {
      destroyTexture(it->second);
      ++m_retiredTextures;
      STAR_RENDER_COUNT(retiredTextures, 1);
      return m_textures.erase(it);
    };
    for (auto it = m_textures.begin(); it != m_textures.end();) {
      // Keep last frame's lighting allocations until replacement versions have
      // had a chance to claim them below. No idle lighting pool is retained.
      if (!it->second.lighting && it->second.source.expired() && it->second.atlasOwner.expired()) it = retire(it);
      else ++it;
    }
    auto& uploads = m_uploadScratch;
    auto& regions = m_regionScratch;
    clearScratch(uploads);
    clearScratch(regions);
    VkDeviceSize totalBytes = 0;
    [[maybe_unused]] size_t reusedImages = 0;
    [[maybe_unused]] size_t reusedLighting = 0;
    size_t createdLighting = 0;
    for (auto const& entry : required) {
      auto const& data = entry.second;
      if (m_textures.count(data->id)) continue;
      // Track ownership before allocating so exceptional exits also release
      // partially created images during backend shutdown.
      auto& texture = m_textures.try_emplace(data->id).first->second;
      if (data->atlasOwner) {
        // All prior frame reads have completed. A slot whose old version is
        // absent from required can be rebound, even if CPU owners retain it.
        for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
          if (!required.count(it->first) && it->second.atlasOwner.lock() == data->atlasOwner) {
            texture = std::move(it->second);
            m_textures.erase(it);
            ++reusedImages;
            break;
          }
        }
      }
      if (data->lighting) {
        // Only effect maps use this pool: RGBA8, linear, clamp, one mip.
        // All prior submissions have completed and required protects EVERY
        // version referenced by the new frame, including same-frame updates.
        for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
          if (it->second.lighting && it->second.size == data->size() && !required.count(it->first)) {
            texture = std::move(it->second);
            m_textures.erase(it);
            ++reusedLighting;
            break;
          }
        }
      }
      if (!texture.image) {
        createTexture(texture, data);
        if (data->lighting) ++createdLighting;
      }
      texture.source = data;
      Upload upload{data.get(), texture.initialized, regions.size(), 0};
      auto region = [&](Image const& pixels, Vec2U offset) {
        regions.push_back({&pixels, offset, totalBytes});
        ++upload.regionCount;
        totalBytes += (VkDeviceSize)pixels.width() * pixels.height() * 4;
      };
      if (data->atlasOwner) {
        constexpr unsigned tile = VulkanTextureData::AtlasTileSize;
        unsigned columns = data->atlasSize[0] / tile;
        for (size_t i = 0; i < data->atlasTiles.size(); ++i) {
          if (!texture.initialized || i >= texture.atlasTiles.size() || texture.atlasTiles[i] != data->atlasTiles[i])
            region(*data->atlasTiles[i], {(unsigned)(i % columns) * tile, (unsigned)(i / columns) * tile});
        }
        texture.atlasTiles = data->atlasTiles;
      } else {
        region(data->pixels, {});
      }
      if (upload.regionCount) uploads.push_back(upload);
    }
    // Retain a bounded number of unused atlas allocations for later frames.
    // Required versions and pages with live draws are never evicted here.
    VkDeviceSize spareBytes = 0;
    for (auto it = m_textures.begin(); it != m_textures.end();) {
      if (it->second.lighting && !required.count(it->first)) {
        it = retire(it);
        continue;
      }
      if (!required.count(it->first) && !it->second.atlasTiles.empty()) {
        auto bytes = (VkDeviceSize)it->second.size[0] * it->second.size[1] * 4;
        if (it->second.atlasOwner.expired() || spareBytes + bytes > 64ULL * 1024 * 1024) {
          it = retire(it);
          continue;
        }
        spareBytes += bytes;
      }
      ++it;
    }
    STAR_RENDER_PLOT("Vulkan reused atlas images/frame", reusedImages);
    STAR_RENDER_PLOT("Vulkan reused lighting images/frame", reusedLighting);
    STAR_RENDER_PLOT("Vulkan new lighting images/frame", createdLighting);
    m_lightingAllocations += createdLighting;
    m_lightingReuses += reusedLighting;
    if (m_verify && m_worldScene && m_frames > 0 && m_frames % 3 == 1
        && m_frames % 7 != 0 && m_frames % 7 != 6 && createdLighting)
      throw RendererException("Vulkan same-size lighting updates allocated instead of reusing completed images");
    STAR_RENDER_PLOT("Vulkan spare atlas payload bytes", spareBytes);
    STAR_RENDER_PLOT("Vulkan uploaded textures/frame", uploads.size());
    STAR_RENDER_PLOT("Vulkan uploaded bytes/frame", totalBytes);
    Switch2FrameProfile::count(Switch2FrameProfile::Counter::UploadBytes, totalBytes);
    STAR_RENDER_PLOT("Vulkan GPU texture cache count", m_textures.size());
    ensureHostBuffer(m_textureStaging, totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (totalBytes) {
      STAR_RENDER_ZONE("Vulkan texture staging and transfer recording");
      for (auto const& region : regions) {
        auto const& pixels = *region.pixels;
        std::memcpy(static_cast<uint8_t*>(m_textureStaging.mapped) + region.bufferOffset,
            pixels.data(), (size_t)pixels.width() * pixels.height() * 4);
      }
      if (!m_textureStaging.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = m_textureStaging.memory;
        range.size = VK_WHOLE_SIZE;
        checkVk(vkFlushMappedMemoryRanges(m_device, 1, &range), "flush texture staging");
      }
      for (auto const& upload : uploads) {
        auto& texture = m_textures.at(upload.data->id);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = upload.initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = upload.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(m_commands, upload.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        auto& copies = m_copyScratch;
        copies.clear();
        for (size_t i = upload.firstRegion; i < upload.firstRegion + upload.regionCount; ++i) {
          auto const& region = regions[i];
          VkBufferImageCopy copy{};
          copy.bufferOffset = region.bufferOffset;
          copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
          copy.imageOffset = {(int32_t)region.offset[0], (int32_t)region.offset[1], 0};
          copy.imageExtent = {region.pixels->width(), region.pixels->height(), 1};
          copies.push_back(copy);
        }
        vkCmdCopyBufferToImage(m_commands, m_textureStaging.buffer, texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)copies.size(), copies.data());
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(m_commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        texture.initialized = true;
        ++m_uploadedTextures;
      }
    }
    clearScratch(uploads);
    clearScratch(regions);
    clearScratch(m_copyScratch);
    STAR_RENDER_NAMED_ZONE(descriptorZone, "Vulkan draw descriptors and uniforms");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physical, &properties);
    auto alignment = properties.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize stride = (sizeof(WorldUniform) + alignment - 1) / alignment * alignment;
    auto& representatives = m_descriptorRepresentatives;
    auto& indices = m_descriptorIndices;
    auto& uniforms = m_uniformScratch;
    clearScratch(representatives);
    clearScratch(indices);
    clearScratch(uniforms);
    // Exact per-frame identity: six ordered immutable texture versions plus
    // the composite target marker and every byte consumed by the world UBO.
    // Transform/scissor are intentionally absent: they are not descriptors.
    using DescriptorKey = std::pair<std::array<uint64_t, 7>, std::array<uint32_t, sizeof(WorldUniform) / sizeof(uint32_t)>>;
    std::map<DescriptorKey, size_t> descriptorCache;
    for (size_t i = 0; i < count; ++i) {
      auto const& draw = m_renderer->draws()[i];
      WorldUniform uniform{};
      if (draw.world) {
        uniform.mapTransform[0] = draw.effect.lightMapScale[0];
        uniform.mapTransform[1] = draw.effect.lightMapScale[1];
        uniform.mapTransform[2] = draw.effect.lightMapOffset[0];
        uniform.mapTransform[3] = draw.effect.lightMapOffset[1];
        uniform.lighting[0] = draw.effect.lightMapEnabled ? 1.0f : 0.0f;
        uniform.lighting[1] = draw.effect.lightMapMultiplier;
        auto lightSize = draw.effect.lightMap ? draw.effect.lightMap->size() : Vec2U(1, 1);
        uniform.lighting[2] = (float)lightSize[0]; uniform.lighting[3] = (float)lightSize[1];
        for (size_t p = 0; p < 6; ++p)
          for (size_t c = 0; c < 3; ++c) uniform.parameters[p][c] = draw.effect.parameters[p][c];
        uniform.screen[0] = (float)m_extent.width; uniform.screen[1] = (float)m_extent.height;
      }
      DescriptorKey key{};
      for (size_t slot = 0; slot < 6; ++slot) {
        auto data = slot < draw.textures.size() ? draw.textures[slot] : m_renderer->whiteTexture();
        if (draw.world && slot >= 4) data = slot == 4 ? draw.effect.lightMap : draw.effect.tileLightMap;
        key.first[slot] = data->id;
      }
      key.first[6] = draw.composite ? 1 : 0;
      std::memcpy(key.second.data(), &uniform, sizeof(uniform));
      auto inserted = descriptorCache.try_emplace(key, representatives.size());
      indices.push_back(inserted.first->second);
      if (inserted.second) {
        representatives.push_back(i);
        uniforms.push_back(uniform);
      }
    }
    size_t descriptorCount = representatives.size();
    Switch2FrameProfile::count(Switch2FrameProfile::Counter::Descriptors, descriptorCount);
    if (m_diagnostic) {
      m_uniqueDescriptors += descriptorCount;
      m_reusedDescriptors += count - descriptorCount;
    }
    STAR_RENDER_PLOT("Vulkan unique descriptors/frame", descriptorCount);
    STAR_RENDER_PLOT("Vulkan reused descriptors/frame", count - descriptorCount);
    bool underused = descriptorCount <= m_descriptorCapacity / 2 && m_descriptorCapacity - descriptorCount > 1024;
    m_descriptorUnderusedFrames = underused ? m_descriptorUnderusedFrames + 1 : 0;
    bool shrinkDescriptors = m_descriptorUnderusedFrames >= 120;
    if (!m_descriptorPool || m_descriptorCapacity < descriptorCount || shrinkDescriptors) {
      if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
      m_descriptorPool = VK_NULL_HANDLE;
      size_t capacity = std::max<size_t>(descriptorCount, 128);
      if (!shrinkDescriptors)
        capacity = std::max(capacity, m_descriptorCapacity + std::min<size_t>(m_descriptorCapacity / 2, 1024));
      m_descriptorCapacity = std::min<size_t>(capacity, UINT32_MAX / 6);
      m_descriptorUnderusedFrames = 0;
      VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)m_descriptorCapacity * 6},
                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)m_descriptorCapacity}};
      VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      pool.maxSets = (uint32_t)m_descriptorCapacity;
      pool.poolSizeCount = 2;
      pool.pPoolSizes = sizes;
      checkVk(vkCreateDescriptorPool(m_device, &pool, nullptr, &m_descriptorPool), "create texture descriptor pool");
    } else {
      checkVk(vkResetDescriptorPool(m_device, m_descriptorPool, 0), "reset texture descriptor pool");
    }
    ensureHostBuffer(m_effectUniforms, stride * descriptorCount, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    clearScratch(m_drawDescriptors);
    m_drawDescriptors.resize(count);
    if (!descriptorCount) return;
    auto& descriptors = m_uniqueDescriptorScratch;
    descriptors.resize(descriptorCount);
    auto& layouts = m_layoutScratch;
    layouts.assign(descriptorCount, m_textureLayout);
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = m_descriptorPool;
    allocation.descriptorSetCount = (uint32_t)descriptorCount;
    allocation.pSetLayouts = layouts.data();
    checkVk(vkAllocateDescriptorSets(m_device, &allocation, descriptors.data()), "allocate unique draw descriptors");
    clearScratch(layouts);
    for (size_t i = 0; i < descriptorCount; ++i) {
      auto const& draw = m_renderer->draws()[representatives[i]];
      auto const& uniform = uniforms[i];
      std::memcpy(static_cast<uint8_t*>(m_effectUniforms.mapped) + stride * i, &uniform, sizeof(uniform));
      VkDescriptorImageInfo images[6]{};
      VkWriteDescriptorSet writes[7]{};
      for (uint32_t slot = 0; slot < 6; ++slot) {
        VulkanTextureDataPtr data = slot < draw.textures.size() ? draw.textures[slot] : m_renderer->whiteTexture();
        if (draw.world && slot >= 4) data = slot == 4 ? draw.effect.lightMap : draw.effect.tileLightMap;
        auto const& texture = m_textures.at(data->id);
        images[slot] = {texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        if (draw.composite && slot == 0) images[slot] = {m_worldTarget.sampler, m_worldTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[slot].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[slot].dstSet = descriptors[i];
        writes[slot].dstBinding = slot;
        writes[slot].descriptorCount = 1;
        writes[slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[slot].pImageInfo = &images[slot];
      }
      VkDescriptorBufferInfo buffer{m_effectUniforms.buffer, stride * i, sizeof(WorldUniform)};
      writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[6].dstSet = descriptors[i]; writes[6].dstBinding = 6;
      writes[6].descriptorCount = 1; writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writes[6].pBufferInfo = &buffer;
      vkUpdateDescriptorSets(m_device, 7, writes, 0, nullptr);
    }
    for (size_t i = 0; i < count; ++i) m_drawDescriptors[i] = descriptors[indices[i]];
    clearScratch(descriptors);
    clearScratch(representatives);
    clearScratch(indices);
    clearScratch(uniforms);
    if (!m_effectUniforms.coherent) {
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = m_effectUniforms.memory; range.size = VK_WHOLE_SIZE;
      checkVk(vkFlushMappedMemoryRanges(m_device, 1, &range), "flush effect parameters");
    }
    if (m_diagnostic && m_textureScene && !m_atlasScene && !m_worldScene && m_textures.size() > 15)
      throw RendererException("Vulkan transient texture retirement failed: cache grew beyond the scene's 15 live textures");
  }

  void createGeometryPipeline(bool world = false) {
    if (!m_layout) {
      VkDescriptorSetLayoutBinding bindings[7]{};
      for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 6 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        if (i == 6) bindings[i].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
      }
      VkDescriptorSetLayoutCreateInfo textureLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      textureLayout.bindingCount = 7;
      textureLayout.pBindings = bindings;
      checkVk(vkCreateDescriptorSetLayout(m_device, &textureLayout, nullptr, &m_textureLayout), "create texture descriptor layout");
      VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushState)};
      VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      layout.setLayoutCount = 1;
      layout.pSetLayouts = &m_textureLayout;
      layout.pushConstantRangeCount = 1;
      layout.pPushConstantRanges = &push;
      checkVk(vkCreatePipelineLayout(m_device, &layout, nullptr, &m_layout), "create geometry layout");
    }
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    try {
      VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      shader.codeSize = world ? sizeof(StarVulkanWorldVertex) : sizeof(StarVulkanFlatVertex);
      shader.pCode = world ? StarVulkanWorldVertex : StarVulkanFlatVertex;
      checkVk(vkCreateShaderModule(m_device, &shader, nullptr, &vertex), "create vertex shader");
      shader.codeSize = world ? sizeof(StarVulkanWorldFragment) : sizeof(StarVulkanFlatFragment);
      shader.pCode = world ? StarVulkanWorldFragment : StarVulkanFlatFragment;
      checkVk(vkCreateShaderModule(m_device, &shader, nullptr, &fragment), "create fragment shader");
      VkPipelineShaderStageCreateInfo stages[2]{};
      for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
      stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
      stages[0].module = vertex;
      stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[1].module = fragment;
      VkVertexInputBindingDescription binding{0, sizeof(VulkanRenderer::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
      VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)(offsetof(VulkanRenderer::Vertex, vertex) + offsetof(RenderVertex, screenCoordinate))},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)(offsetof(VulkanRenderer::Vertex, vertex) + offsetof(RenderVertex, color))},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)(offsetof(VulkanRenderer::Vertex, vertex) + offsetof(RenderVertex, textureCoordinate))},
        {3, 0, VK_FORMAT_R32_UINT, (uint32_t)offsetof(VulkanRenderer::Vertex, textureIndex)},
        {4, 0, VK_FORMAT_R32_SFLOAT, (uint32_t)(offsetof(VulkanRenderer::Vertex, vertex) + offsetof(RenderVertex, param1))}
      };
      VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
      input.vertexBindingDescriptionCount = 1;
      input.pVertexBindingDescriptions = &binding;
      input.vertexAttributeDescriptionCount = world ? 5 : 4;
      input.pVertexAttributeDescriptions = attributes;
      VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
      assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
      viewport.viewportCount = 1;
      viewport.scissorCount = 1;
      VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
      raster.polygonMode = VK_POLYGON_MODE_FILL;
      raster.cullMode = VK_CULL_MODE_NONE;
      raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
      raster.lineWidth = 1;
      VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
      samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      VkPipelineColorBlendAttachmentState blend{};
      blend.blendEnable = VK_TRUE;
      blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend.colorBlendOp = VK_BLEND_OP_ADD;
      // Match glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA) for alpha as well.
      blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend.alphaBlendOp = VK_BLEND_OP_ADD;
      blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      // The OpenGL world target is RGB, so its alpha must remain opaque when
      // composited. Keep the Vulkan RGBA target's cleared alpha of 1 intact.
      if (world) blend.colorWriteMask &= ~VK_COLOR_COMPONENT_A_BIT;
      VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
      blending.attachmentCount = 1;
      blending.pAttachments = &blend;
      VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
      VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
      dynamic.dynamicStateCount = 2;
      dynamic.pDynamicStates = states;
      VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
      pipeline.stageCount = 2;
      pipeline.pStages = stages;
      pipeline.pVertexInputState = &input;
      pipeline.pInputAssemblyState = &assembly;
      pipeline.pViewportState = &viewport;
      pipeline.pRasterizationState = &raster;
      pipeline.pMultisampleState = &samples;
      pipeline.pColorBlendState = &blending;
      pipeline.pDynamicState = &dynamic;
      pipeline.layout = m_layout;
      pipeline.renderPass = m_renderPass;
      checkVk(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline, nullptr, world ? &m_worldPipeline : &m_pipeline), "create graphics pipeline");
    } catch (...) {
      if (vertex) vkDestroyShaderModule(m_device, vertex, nullptr);
      if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
      throw;
    }
    vkDestroyShaderModule(m_device, vertex, nullptr);
    vkDestroyShaderModule(m_device, fragment, nullptr);
  }

  void recordGeometry(uint32_t imageIndex, VkClearValue const& clear) {
    Switch2FrameProfile::Zone recordZone(Switch2FrameProfile::Phase::Record);
    STAR_RENDER_ZONE("Vulkan record draw commands");
    auto beginPass = [&](VkRenderPass pass, VkFramebuffer framebuffer, VkClearValue const& color) {
      VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      render.renderPass = pass; render.framebuffer = framebuffer; render.renderArea.extent = m_extent;
      render.clearValueCount = 1; render.pClearValues = &color;
      vkCmdBeginRenderPass(m_commands, &render, VK_SUBPASS_CONTENTS_INLINE);
    };
    bool active = false, activeWorld = false, swapInitialized = false, worldInitialized = false;
    VkClearValue black{}; black.color.float32[3] = 1;
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundDescriptor = VK_NULL_HANDLE;
    PushState boundPush{};
    bool pushSet = false;
    VkRect2D boundScissor{};
    bool scissorSet = false;
    VkViewport viewport{0, 0, (float)m_extent.width, (float)m_extent.height, 0, 1};
    vkCmdSetViewport(m_commands, 0, 1, &viewport);
    VkDeviceSize offset = 0;
    if (!m_renderer->vertices().empty()) vkCmdBindVertexBuffers(m_commands, 0, 1, &m_vertices.buffer, &offset);
    size_t drawIndex = 0;
    for (auto const& draw : m_renderer->draws()) {
      auto descriptor = m_drawDescriptors[drawIndex++];
      // Empty-world composition still samples opaque black. Defer this
      // clear-only pass until it is actually needed; normal world draws clear
      // in their first pass instead of storing then immediately loading it.
      if (draw.composite && !worldInitialized) {
        if (active) vkCmdEndRenderPass(m_commands);
        beginPass(m_worldClearPass, m_worldFramebuffer, black);
        vkCmdEndRenderPass(m_commands);
        worldInitialized = true;
        active = false;
      }
      if (!active || activeWorld != draw.world) {
        if (active) vkCmdEndRenderPass(m_commands);
        if (draw.world) {
          beginPass(worldInitialized ? m_worldLoadPass : m_worldClearPass, m_worldFramebuffer, black);
          worldInitialized = true;
        }
        else {
          beginPass(swapInitialized ? m_swapLoadPass : m_renderPass, m_framebuffers[imageIndex], clear);
          swapInitialized = true;
        }
        active = true; activeWorld = draw.world;
        boundPipeline = VK_NULL_HANDLE;
        boundDescriptor = VK_NULL_HANDLE;
        pushSet = false;
        scissorSet = false;
      }
      VkRect2D scissor{{0, 0}, m_extent};
      if (draw.scissor) {
        auto clip = [&](int64_t value, uint32_t maximum) { return (uint32_t)std::clamp<int64_t>(value, 0, maximum); };
        auto x0 = clip(draw.scissor->xMin(), m_extent.width), x1 = clip(draw.scissor->xMax(), m_extent.width);
        auto y0 = clip(draw.scissor->yMin(), m_extent.height), y1 = clip(draw.scissor->yMax(), m_extent.height);
        if (x1 <= x0 || y1 <= y0) continue;
        scissor.offset = {(int32_t)x0, (int32_t)(m_extent.height - y1)};
        scissor.extent = {x1 - x0, y1 - y0};
      }
      auto pipeline = draw.world ? m_worldPipeline : m_pipeline;
      if (boundPipeline != pipeline) {
        vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundPipeline = pipeline;
      }
      if (!scissorSet || scissor.offset.x != boundScissor.offset.x || scissor.offset.y != boundScissor.offset.y
          || scissor.extent.width != boundScissor.extent.width || scissor.extent.height != boundScissor.extent.height) {
        vkCmdSetScissor(m_commands, 0, 1, &scissor);
        boundScissor = scissor;
        scissorSet = true;
      }
      if (boundDescriptor != descriptor) {
        vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &descriptor, 0, nullptr);
        boundDescriptor = descriptor;
      }
      PushState push{};
      for (size_t row = 0; row < 3; ++row)
        for (size_t column = 0; column < 3; ++column)
          push.rows[row][column] = draw.transform[row][column];
      push.screen[0] = (float)m_extent.width;
      push.screen[1] = (float)m_extent.height;
      if (!pushSet || std::memcmp(&push, &boundPush, sizeof(push)) != 0) {
        vkCmdPushConstants(m_commands, m_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        boundPush = push;
        pushSet = true;
      }
      vkCmdDraw(m_commands, draw.vertexCount, 1, draw.firstVertex, 0);
    }
    if (active) vkCmdEndRenderPass(m_commands);
    if (!swapInitialized) {
      beginPass(m_renderPass, m_framebuffers[imageIndex], clear);
      vkCmdEndRenderPass(m_commands);
    }
  }

  void recordReadback(uint32_t index) {
    ensureHostBuffer(m_readback, (VkDeviceSize)m_extent.width * m_extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_images[index];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {m_extent.width, m_extent.height, 1};
    vkCmdCopyImageToBuffer(m_commands, m_images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback.buffer, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(m_commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = m_readback.buffer;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(m_commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
  }

  void verifyGeometry() {
    if (!m_readback.coherent) {
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = m_readback.memory;
      range.size = VK_WHOLE_SIZE;
      checkVk(vkInvalidateMappedMemoryRanges(m_device, 1, &range), "invalidate readback");
    }
    auto bytes = static_cast<uint8_t const*>(m_readback.mapped);
    auto pixel = [&](uint32_t x, uint32_t y) {
      auto p = bytes + ((size_t)(m_extent.height - 1 - y) * m_extent.width + x) * 4;
      if (m_format == VK_FORMAT_B8G8R8A8_UNORM) return Vec4B(p[2], p[1], p[0], p[3]);
      return Vec4B(p[0], p[1], p[2], p[3]);
    };
    // Save evidence on swapchain transitions, not every atlas verification
    // frame: PNG encoding would dominate the dynamic mutation regression.
    if (m_verifyNext) {
      Image screenshot(Vec2U(m_extent.width, m_extent.height));
      for (uint32_t y = 0; y < m_extent.height; ++y)
        for (uint32_t x = 0; x < m_extent.width; ++x)
          screenshot.set({x, y}, pixel(x, y));
      screenshot.writePng(File::open(m_worldScene ? "vulkan-world.png" : m_atlasScene ? "vulkan-atlas.png" : m_textureScene ? "vulkan-textures.png" : "vulkan-geometry.png", IOMode::Write | IOMode::Truncate));
    }
    unsigned checks = 0;
    auto probe = [&](char const* name, unsigned x, unsigned y, Vec4B expected) {
      auto actual = pixel((uint32_t)(x * m_extent.width / 960), (uint32_t)(y * m_extent.height / 540));
      for (size_t c = 0; c < 4; ++c)
        if (std::abs((int)actual[c] - (int)expected[c]) > 3)
          throw RendererException::format("Vulkan pixel check '{}' failed: actual {}, expected {}", name, actual, expected);
      ++checks;
    };
    if (m_worldScene && m_frames % 7 == 6) {
      probe("empty world clears prior contents", 940, 510, {0, 0, 0, 255});
      probe("repeated scissor", 110, 80, {0, 0, 255, 255});
      probe("scissor excludes outside", 190, 80, {0, 0, 0, 255});
      probe("scissor restored", 280, 80, {255, 255, 0, 255});
    } else if (m_worldScene) {
      probe("world composition replaces prior UI", 940, 510, {20, 30, 40, 255});
      probe("lightmap old snapshot", 90, 380, m_frames % 2 ? Vec4B(50, 80, 120, 255) : Vec4B(100, 40, 120, 255));
      probe("lightmap replacement", 380, 420, {50, 160, 60, 255});
      probe("multiplier snapshot", 620, 420, {25, 80, 30, 255});
      probe("per-vertex fullbright", 840, 420, {200, 160, 120, 255});
      probe("premultiplied tile light", 140, 230, {25, 120, 30, 255});
      probe("alpha-254 emissive", 380, 230, {200, 100, 50, 255});
      probe("brightness", 620, 230, {100, 125, 150, 255});
      probe("desaturation", 840, 230, {118, 118, 118, 255});
      probe("RGB adjustment", 110, 80, {100, 160, 30, 255});
      probe("lighting disabled", 280, 80, {200, 160, 120, 255});
      probe("RGB target alpha semantics", 450, 80, {110, 65, 45, 255});
      probe("UI above world", 160, 420, m_frames % 2 ? Vec4B(153, 168, 188, 191) : Vec4B(178, 148, 188, 191));
      probe("compose only once", 910, 20, {20, 30, 40, 255});
      probe("empty startup lightmaps", 910, 80, {70, 100, 140, 255});
      // Independent 16-tap cubic B-spline reference, rather than the shader's
      // optimized four bilinear samples, checks spatial lightmap coordinates.
      auto cubicReference = [&](unsigned x, unsigned y, float offset) {
        double px = ((uint32_t)(x * m_extent.width / 960) + 0.5) * 2 / m_extent.width - offset - 0.5;
        double py = ((uint32_t)(y * m_extent.height / 540) + 0.5) * 2 / m_extent.height - offset - 0.5;
        int ix = (int)std::floor(px), iy = (int)std::floor(py);
        auto weights = [](double f) { return std::array<double, 4>{std::pow(1-f, 3)/6, (3*f*f*f-6*f*f+4)/6, (-3*f*f*f+3*f*f+3*f+1)/6, f*f*f/6}; };
        auto wx = weights(px - ix), wy = weights(py - iy);
        auto source = diagnosticLightMap();
        Vec4B result(0, 0, 0, 255);
        for (unsigned c = 0; c < 3; ++c) {
          double sum = 0;
          for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i)
            sum += source.getrgb(std::clamp(ix+i-1, 0, 1), std::clamp(iy+j-1, 0, 1))[c] * wx[i] * wy[j];
          result[c] = (uint8_t)std::round(sum);
        }
        return result;
      };
      probe("bicubic spatial sampling", 620, 80, cubicReference(620, 80, 0));
      probe("lightmap offset and scale", 790, 80, cubicReference(790, 80, 0.5f));
    } else if (m_atlasScene) {
      probe("atlas nearest bottom-left", 90, 380, {255, 0, 0, 255});
      probe("atlas nearest bottom-right", 190, 380, {0, 255, 0, 255});
      probe("atlas nearest top-left", 90, 460, {0, 0, 255, 255});
      probe("atlas nearest top-right", 190, 460, {255, 255, 0, 255});
      probe("atlas linear center", 380, 420, {128, 128, 64, 255});
      probe("atlas linear border replication", 285, 345, {255, 0, 0, 255});
      probe("second atlas page", 620, 420, atlasTileColor(269));
      probe("oversized standalone fallback", 840, 420, {240, 150, 30, 255});
      probe("freed region old snapshot", 140, 230, atlasOldColor(m_frames));
      probe("reused region new snapshot", 380, 230, atlasNewColor(m_frames));
      probe("persistent cached atlas UV bottom-left", 570, 195, {255, 0, 0, 255});
      probe("persistent cached atlas UV top-right", 670, 265, {255, 255, 0, 255});
      probe("destroyed group pending draw", 840, 230, {190, 60, 220, 255});
      unsigned indices[]{0, 1, 63, 128, 200, 254, 255, 269};
      for (unsigned i = 0; i < 8; ++i) probe("atlas cell/page offset", 80 + i * 110, 80, atlasTileColor(indices[i]));
    } else if (m_textureScene) {
      probe("nearest bottom-left red", 90, 380, {255, 0, 0, 255});
      probe("nearest bottom-right green", 190, 380, {0, 255, 0, 255});
      probe("nearest top-left blue", 90, 460, {0, 0, 255, 255});
      probe("nearest top-right yellow", 190, 460, {255, 255, 0, 255});
      probe("linear center", 380, 420, {128, 128, 64, 255});
      probe("linear clamp edge", 285, 345, {255, 0, 0, 255});
      probe("negative wrap red", 535, 355, {255, 0, 0, 255});
      probe("negative wrap green", 560, 355, {0, 255, 0, 255});
      probe("negative wrap blue", 535, 375, {0, 0, 255, 255});
      probe("clamp bottom-left", 770, 350, {255, 0, 0, 255});
      probe("clamp bottom-right", 910, 350, {0, 255, 0, 255});
      probe("clamp top-left", 770, 490, {0, 0, 255, 255});
      probe("clamp top-right", 910, 490, {255, 255, 0, 255});
      probe("transparent texel", 90, 195, {80, 80, 80, 255});
      probe("half-alpha texel", 190, 195, {168, 168, 168, 191});
      probe("opaque texel", 90, 265, {255, 255, 255, 255});
      probe("quarter-alpha texel", 190, 265, {124, 124, 124, 207});
      probe("vertex tint red", 330, 195, {128, 0, 0, 255});
      probe("vertex tint yellow", 430, 265, {128, 255, 0, 255});
      probe("RGB24 odd-width upload", 560, 220, {20, 80, 200, 255});
      probe("BGR24 odd-width upload", 670, 220, {200, 80, 20, 255});
      probe("BGRA32 half-alpha upload", 780, 220, {23, 123, 72, 191});
      probe("released texture and mutated source", 885, 220, {245, 210, 40, 255});
      probe("cached texture snapshot", 110, 320, {255, 0, 0, 255});
      probe("updated cached texture", 220, 320, {0, 255, 0, 255});
      Vec4B colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
                        {255, 255, 0, 255}, {255, 0, 255, 255}, {0, 255, 255, 255}};
      for (unsigned i = 0; i < 12; ++i) probe("texture slot and batch boundary", 70 + i * 74, 80, colors[i % 6]);
      probe("textured scissor inside", 480, 18, {255, 0, 255, 255});
      probe("textured scissor outside", 380, 18, {6, 26, 64, 255});
      size_t maxSlots = 0;
      for (auto const& draw : m_renderer->draws()) maxSlots = std::max(maxSlots, draw.textures.size());
      if (maxSlots != (m_singleTexture ? 1u : 4u))
        throw RendererException("Texture diagnostic did not exercise the expected batch slot count");
    } else {
    probe("background", 940, 510, {6, 26, 64, 255});
    probe("bottom-left coordinates", 100, 100, {230, 70, 60, 255});
    probe("triangle", 170, 400, {70, 220, 140, 255});
    probe("alpha overlap and draw order", 230, 200, {135, 100, 150, 191});
    probe("transformed cached buffer snapshot", 490, 210, {245, 200, 55, 255});
    probe("rotated corner", 540, 260, {6, 26, 64, 255});
    probe("updated cached buffer", 510, 410, {215, 80, 210, 255});
    probe("scaled buffer boundary", 552, 410, {6, 26, 64, 255});
    probe("polygon triangulation", 750, 410, {60, 210, 220, 255});
    probe("scissor inside", 700, 200, {80, 220, 130, 255});
    probe("scissor outside", 630, 200, {6, 26, 64, 255});
    probe("scissor reset", 905, 320, {255, 255, 255, 255});
    probe("negative oversized scissor", 20, 20, {255, 160, 40, 255});
    }
    ++m_verifiedFrames;
    Logger::info("{}: {} GPU pixel checks passed at {}x{}", m_worldScene ? "World" : m_atlasScene ? "Atlas" : m_textureScene ? "Textures" : "Geometry", checks, m_extent.width, m_extent.height);
  }

  template <typename T> T global(char const* name) {
    auto fn = reinterpret_cast<T>(m_getInstanceProc(VK_NULL_HANDLE, name));
    if (!fn) throw RendererException::format("Vulkan entry point missing: {}", name);
    return fn;
  }
  template <typename T> T instance(char const* name) {
    auto fn = reinterpret_cast<T>(m_getInstanceProc(m_instance, name));
    if (!fn) throw RendererException::format("Vulkan instance entry point missing: {}", name);
    return fn;
  }
  template <typename T> T device(char const* name) {
    auto fn = reinterpret_cast<T>(vkGetDeviceProcAddr(m_device, name));
    if (!fn) throw RendererException::format("Vulkan device entry point missing: {}", name);
    return fn;
  }
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
      VkDebugUtilsMessageTypeFlagsEXT, VkDebugUtilsMessengerCallbackDataEXT const* data, void* user) {
    // A driver callback must not allow exceptions to cross the C ABI boundary.
    try {
      if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++static_cast<SdlVulkanDiagnostic*>(user)->m_validationErrors;
        Logger::error("Vulkan validation: {}", data->pMessage);
      } else {
        Logger::warn("Vulkan validation: {}", data->pMessage);
      }
    } catch (...) {}
    return VK_FALSE;
  }
  bool deviceHasExtension(VkPhysicalDevice physical, char const* name) {
    uint32_t count = 0;
    checkVk(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr), "enumerate device extensions");
    std::vector<VkExtensionProperties> extensions(count);
    checkVk(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()), "enumerate device extensions");
    return std::any_of(extensions.begin(), extensions.end(), [&](auto const& ext) { return std::strcmp(ext.extensionName, name) == 0; });
  }
  void chooseDevice() {
    uint32_t count = 0;
    checkVk(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "enumerate physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    checkVk(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "enumerate physical devices");
    for (auto physical : devices) {
      if (!deviceHasExtension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
      uint32_t graphicsFamily = UINT32_MAX, presentFamily = UINT32_MAX;
      for (uint32_t i = 0; i < familyCount; ++i) {
        VkBool32 presentSupported = VK_FALSE;
        checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, m_surface, &presentSupported), "query presentation support");
        if (!families[i].queueCount) continue;
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily = i;
        if (presentSupported) presentFamily = i;
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) break;
      }
      if (graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX) {
        uint32_t formats = 0, modes = 0;
        checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, m_surface, &formats, nullptr), "query candidate formats");
        checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, m_surface, &modes, nullptr), "query candidate present modes");
        if (!formats || !modes) continue;
        m_physical = physical;
        m_queueFamily = graphicsFamily;
        m_presentFamily = presentFamily;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        Logger::info("Vulkan: selected device '{}' (API {}.{}.{}, graphics queue {}, present queue {})", properties.deviceName,
            VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion), graphicsFamily, presentFamily);
        return;
      }
    }
    throw RendererException("No Vulkan device with graphics/presentation queues, surface formats, and VK_KHR_swapchain was found");
  }

  void destroySwapchain() {
    if (m_worldFramebuffer) vkDestroyFramebuffer(m_device, m_worldFramebuffer, nullptr);
    m_worldFramebuffer = VK_NULL_HANDLE;
    destroyTexture(m_worldTarget);
    if (m_worldPipeline) vkDestroyPipeline(m_device, m_worldPipeline, nullptr);
    m_worldPipeline = VK_NULL_HANDLE;
    for (auto pass : {m_worldClearPass, m_worldLoadPass, m_swapLoadPass})
      if (pass) vkDestroyRenderPass(m_device, pass, nullptr);
    m_worldClearPass = m_worldLoadPass = m_swapLoadPass = VK_NULL_HANDLE;
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    for (auto framebuffer : m_framebuffers) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    m_framebuffers.clear();
    for (auto view : m_views) vkDestroyImageView(m_device, view, nullptr);
    m_views.clear();
    for (auto semaphore : m_finished) vkDestroySemaphore(m_device, semaphore, nullptr);
    m_finished.clear();
    if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
    if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
  }

  void recreateSwapchain(int width, int height) {
    STAR_RENDER_ZONE("Vulkan recreate swapchain (includes device idle)");
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Vulkan, "device idle before swapchain recreation");
    checkVk(vkDeviceWaitIdle(m_device), "wait before swapchain recreation");
    destroySwapchain();
    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &capabilities), "query surface capabilities");
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      throw RendererException("Vulkan surface does not support color attachments");
    m_extent = capabilities.currentExtent;
    if (m_extent.width == UINT32_MAX) {
      m_extent.width = std::clamp((uint32_t)width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
      m_extent.height = std::clamp((uint32_t)height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    // Some window systems report a zero currentExtent while minimized even
    // when SDL's cached drawable size is still nonzero. Retry next iteration.
    if (!m_extent.width || !m_extent.height) return;
    uint32_t count = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &count, nullptr), "query surface formats");
    if (!count) throw RendererException("Vulkan surface has no formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &count, formats.data()), "query surface formats");
    VkSurfaceFormatKHR format = formats.front();
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (auto candidate : formats)
      if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) format = candidate;
    m_format = format.format;
    if (m_verify && (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        || (m_format != VK_FORMAT_B8G8R8A8_UNORM && m_format != VK_FORMAT_R8G8B8A8_UNORM)))
      throw RendererException("Geometry verification requires a readable RGBA8/BGRA8 UNORM swapchain");
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &count, nullptr), "query present modes");
    std::vector<VkPresentModeKHR> modes(count);
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &count, modes.data()), "query present modes");
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_vsync) {
      for (auto candidate : modes) if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) mode = candidate;
      for (auto candidate : modes) if (candidate == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = candidate;
    }
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
      if (capabilities.supportedCompositeAlpha & candidate) { alpha = candidate; break; }
    }
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount) imageCount = std::min(imageCount, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR swapchain{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain.surface = m_surface;
    swapchain.minImageCount = imageCount;
    swapchain.imageFormat = format.format;
    swapchain.imageColorSpace = format.colorSpace;
    swapchain.imageExtent = m_extent;
    swapchain.imageArrayLayers = 1;
    swapchain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (m_verify) swapchain.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapchain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    uint32_t queueFamilies[] = {m_queueFamily, m_presentFamily};
    if (m_queueFamily != m_presentFamily) {
      swapchain.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      swapchain.queueFamilyIndexCount = 2;
      swapchain.pQueueFamilyIndices = queueFamilies;
    }
    swapchain.preTransform = capabilities.currentTransform;
    swapchain.compositeAlpha = alpha;
    swapchain.presentMode = mode;
    swapchain.clipped = VK_TRUE;
    checkVk(vkCreateSwapchainKHR(m_device, &swapchain, nullptr, &m_swapchain), "create swapchain");
    checkVk(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr), "get swapchain images");
    std::vector<VkImage> images(count);
    checkVk(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, images.data()), "get swapchain images");
    m_images = images;

    createScenePass(false, false, m_renderPass);
    createScenePass(false, true, m_swapLoadPass);
    createGeometryPipeline();
    if (m_renderer && m_renderer->worldEnabled()) {
      createScenePass(true, false, m_worldClearPass);
      createScenePass(true, true, m_worldLoadPass);
      createGeometryPipeline(true);
      createTexture(m_worldTarget, m_renderer->whiteTexture(), {m_extent.width, m_extent.height});
      VkFramebufferCreateInfo world{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      world.renderPass = m_worldClearPass; world.attachmentCount = 1; world.pAttachments = &m_worldTarget.view;
      world.width = m_extent.width; world.height = m_extent.height; world.layers = 1;
      checkVk(vkCreateFramebuffer(m_device, &world, nullptr, &m_worldFramebuffer), "create world framebuffer");
    }
    for (auto image : images) {
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = image;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = format.format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.layerCount = 1;
      VkImageView handle = VK_NULL_HANDLE;
      checkVk(vkCreateImageView(m_device, &view, nullptr, &handle), "create swapchain view");
      m_views.push_back(handle);
      VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      framebuffer.renderPass = m_renderPass;
      framebuffer.attachmentCount = 1;
      framebuffer.pAttachments = &m_views.back();
      framebuffer.width = m_extent.width;
      framebuffer.height = m_extent.height;
      framebuffer.layers = 1;
      VkFramebuffer fb = VK_NULL_HANDLE;
      checkVk(vkCreateFramebuffer(m_device, &framebuffer, nullptr, &fb), "create framebuffer");
      m_framebuffers.push_back(fb);
      VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VkSemaphore finished = VK_NULL_HANDLE;
      checkVk(vkCreateSemaphore(m_device, &semaphore, nullptr, &finished), "create present semaphore");
      m_finished.push_back(finished);
    }
    m_dirty = false;
    m_verifyNext = true;
    Logger::info("Vulkan: swapchain {}x{}, {} images, present mode {}", m_extent.width, m_extent.height, images.size(), (int)mode);
  }

  void createScenePass(bool world, bool load, VkRenderPass& pass) {
    VkAttachmentDescription attachment{};
    attachment.format = m_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.finalLayout = world ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.initialLayout = load ? attachment.finalLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    dependencies[1].srcSubpass = 0; dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo renderPass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPass.attachmentCount = 1;
    renderPass.pAttachments = &attachment;
    renderPass.subpassCount = 1;
    renderPass.pSubpasses = &subpass;
    renderPass.dependencyCount = 2;
    renderPass.pDependencies = dependencies;
    checkVk(vkCreateRenderPass(m_device, &renderPass, nullptr, &pass), "create scene render pass");
  }

#define DECLARE_FUNCTION(name) PFN_##name name = nullptr;
  INSTANCE_FUNCTIONS(DECLARE_FUNCTION)
  DEVICE_FUNCTIONS(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION
  PFN_vkGetInstanceProcAddr m_getInstanceProc = nullptr;
  PFN_vkDestroyDebugUtilsMessengerEXT m_destroyDebug = nullptr;
  SDL_Window* m_window = nullptr;
  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkPhysicalDevice m_physical = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  uint32_t m_queueFamily = 0;
  uint32_t m_presentFamily = 0;
  VkQueue m_queue = VK_NULL_HANDLE;
  VkQueue m_presentQueue = VK_NULL_HANDLE;
  VkCommandPool m_pool = VK_NULL_HANDLE;
  VkCommandBuffer m_commands = VK_NULL_HANDLE;
  VkSemaphore m_acquired = VK_NULL_HANDLE;
  VkFence m_fence = VK_NULL_HANDLE;
  VkQueryPool m_benchmarkQueries = VK_NULL_HANDLE;
  PFN_vkDestroyQueryPool m_destroyBenchmarkQueries = nullptr;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkPipeline m_worldPipeline = VK_NULL_HANDLE;
  VkRenderPass m_worldClearPass = VK_NULL_HANDLE, m_worldLoadPass = VK_NULL_HANDLE, m_swapLoadPass = VK_NULL_HANDLE;
  VkFramebuffer m_worldFramebuffer = VK_NULL_HANDLE;
  GpuTexture m_worldTarget;
  HostBuffer m_effectUniforms;
  VkPipelineLayout m_layout = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  HostBuffer m_vertices;
  HostBuffer m_readback;
  shared_ptr<VulkanRenderer> m_renderer;
  TextureFixtures m_textureFixtures;
  AtlasFixtures m_atlasFixtures;
  WorldFixtures m_worldFixtures;
  std::map<uint64_t, GpuTexture> m_textures;
  HostBuffer m_textureStaging;
  VkDescriptorSetLayout m_textureLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  size_t m_descriptorCapacity = 0;
  unsigned m_descriptorUnderusedFrames = 0;
  std::vector<VkDescriptorSet> m_drawDescriptors;
  std::vector<VkDescriptorSet> m_uniqueDescriptorScratch;
  std::vector<size_t> m_descriptorRepresentatives, m_descriptorIndices;
  std::vector<WorldUniform> m_uniformScratch;
  std::vector<Upload> m_uploadScratch;
  std::vector<UploadRegion> m_regionScratch;
  std::vector<VkBufferImageCopy> m_copyScratch;
  std::vector<VkDescriptorSetLayout> m_layoutScratch;
  bool m_textureScene = true;
  bool m_singleTexture = false;
  bool m_diagnostic = true;
  bool m_gameplay = false;
  bool m_atlasScene = false;
  bool m_worldScene = false;
  Vec2U m_lastDrawableSize{};
  uint64_t m_uniqueDescriptors = 0, m_reusedDescriptors = 0;
  uint64_t m_uploadedTextures = 0;
  uint64_t m_retiredTextures = 0;
  uint64_t m_lightingAllocations = 0, m_lightingReuses = 0;
  std::vector<VkImage> m_images;
  VkExtent2D m_extent{};
  std::vector<VkImageView> m_views;
  std::vector<VkFramebuffer> m_framebuffers;
  std::vector<VkSemaphore> m_finished;
  std::atomic<unsigned> m_validationErrors{0};
  uint64_t m_frames = 0;
  bool m_vsync = true;
  bool m_dirty = true;
  bool m_verify = false;
  bool m_verifyNext = true;
  uint64_t m_verifiedFrames = 0;
};

#undef INSTANCE_FUNCTIONS
#undef DEVICE_FUNCTIONS

}

unique_ptr<SdlRenderBackend> createSdlVulkanMenuBackend(bool gameplay) {
  return make_unique<SdlVulkanDiagnostic>(false, gameplay);
}

int runVulkanDiagnostic(StringList const& arguments) {
  // Logging is independent of Root and xsbinit.config, so failures before game
  // startup have an unambiguous location beside the executable.
#ifndef __SWITCH2__
  if (char* basePath = SDL_GetBasePath()) {
    File::changeDirectory(basePath);
    SDL_free(basePath);
  }
#endif
  auto log = make_shared<FileLogSink>("vulkan-diagnostic.log", LogLevel::Info, true);
  Logger::addSink(log);
  int status = 0;
  try {
    bool benchmark = false;
    bool smoke = false;
    bool verify = false;
    bool geometry = false, singleTexture = false, atlas = false, world = false;
    uint64_t limit = 0;
    for (size_t i = 1; i < arguments.size(); ++i) {
      if (arguments[i] == "--vulkan-smoke-test") {
        smoke = true;
        verify = true;
        limit = 180;
      } else if (arguments[i] == "--vulkan-benchmark") {
        benchmark = true;
      } else if (arguments[i] == "--vulkan-verify") {
        verify = true;
      } else if (arguments[i] == "--vulkan-geometry") {
        geometry = true;
      } else if (arguments[i] == "--vulkan-single-texture") {
        singleTexture = true;
      } else if (arguments[i] == "--vulkan-atlas") {
        atlas = true;
      } else if (arguments[i] == "--vulkan-world") {
        world = true;
      } else if (arguments[i].beginsWith("--vulkan-frames=")) {
        auto value = arguments[i].substr(16).utf8();
        size_t end = 0;
        limit = std::stoull(value, &end);
        if (!limit || limit > 100000 || end != value.size())
          throw RendererException("--vulkan-frames must be between 1 and 100000");
      } else {
        throw RendererException::format("Unknown Vulkan diagnostic argument: {}", arguments[i]);
      }
    }
    if ((int)atlas + (int)geometry + (int)world > 1) throw RendererException("Select one Vulkan diagnostic scene");
    Logger::info("Vulkan {} diagnostic: no game assets or saves are loaded. Escape quits; V toggles VSync; F11 toggles fullscreen.", world ? "world" : atlas ? "atlas" : geometry ? "geometry" : "textures");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
      throw RendererException::format("SDL video initialization: {}", SDL_GetError());
    struct QuitSdl { ~QuitSdl() { SDL_Quit(); } } quitSdl;
    unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("xStarbound Vulkan diagnostic | V: VSync | F11: fullscreen | Esc: quit",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540, VulkanWindowFlags | SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    if (!window) throw RendererException::format("SDL Vulkan window: {}", SDL_GetError());
    // Backend is destroyed before window and SDL, including exceptional exits.
    SdlVulkanDiagnostic backend;
    if (verify) backend.enableVerification();
    if (geometry) backend.useGeometryScene();
    if (singleTexture) backend.useSingleTextureBatches();
    if (atlas) backend.useAtlasScene();
    if (world) backend.useWorldScene();
    backend.initialize(window.get());
    backend.createRenderer();
    bool quit = false, vsync = true, fullscreen = false;
    uint64_t iterations = 0;
    Uint32 started = SDL_GetTicks();
    while (!quit && (!limit || iterations < limit)) {
#ifdef __SWITCH2__
      if (!appletMainLoop()) break;
#endif
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) quit = true;
        if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
            || event.window.event == SDL_WINDOWEVENT_RESTORED || event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED))
          backend.resize();
        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
          if (event.key.keysym.sym == SDLK_ESCAPE) quit = true;
          if (event.key.keysym.sym == SDLK_v) backend.setVSyncEnabled(vsync = !vsync);
          if (event.key.keysym.sym == SDLK_F11) {
            fullscreen = !fullscreen;
            if (SDL_SetWindowFullscreen(window.get(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0))
              throw RendererException::format("SDL fullscreen: {}", SDL_GetError());
            backend.resize();
          }
        }
      }
      if (quit) break;
      if (smoke) {
        if (iterations == 20) { Logger::info("Smoke: resize"); SDL_SetWindowSize(window.get(), 800, 600); backend.resize(); }
        if (iterations == 40) { Logger::info("Smoke: VSync off"); backend.setVSyncEnabled(false); }
        if (iterations == 60) {
          Logger::info("Smoke: fullscreen");
          if (SDL_SetWindowFullscreen(window.get(), SDL_WINDOW_FULLSCREEN_DESKTOP)) throw RendererException(SDL_GetError());
          backend.resize();
        }
        if (iterations == 80) {
          Logger::info("Smoke: windowed");
          if (SDL_SetWindowFullscreen(window.get(), 0)) throw RendererException(SDL_GetError());
          backend.resize();
        }
        if (iterations == 100) { Logger::info("Smoke: minimize"); SDL_MinimizeWindow(window.get()); }
        if (iterations == 110) { Logger::info("Smoke: restore"); SDL_RestoreWindow(window.get()); backend.resize(); }
        if (iterations == 130) { Logger::info("Smoke: VSync on"); backend.setVSyncEnabled(true); }
      }
      backend.present();
      ++iterations;
      SDL_Delay(10);
      if (limit && SDL_GetTicks() - started > 30000)
        throw RendererException("Vulkan diagnostic exceeded its 30-second test deadline");
    }
    if (benchmark && !quit && (!limit || iterations >= limit)) backend.runOffscreenBenchmark();
    // Include validation messages emitted during resource teardown in the
    // result, and tear down graphics before SDL destroys the native window.
    backend.shutdown();
    Logger::info("Vulkan diagnostic completed: {} presented frames, {} verified frames, {} validation errors", backend.framesPresented(), backend.verifiedFrames(), backend.validationErrors());
    Logger::info("Vulkan descriptors: {} unique, {} reused", backend.uniqueDescriptors(), backend.reusedDescriptors());
    Logger::info("Vulkan textures: {} uploaded, {} retired after frame fences", backend.uploadedTextures(), backend.retiredTextures());
    if (!backend.framesPresented() || backend.validationErrors() || (verify && !backend.verifiedFrames()) || (limit && iterations < limit)) status = 1;
  } catch (std::exception const& e) {
    Logger::error("Vulkan diagnostic failed: {}", outputException(e, true));
    status = 1;
  }
  Logger::removeSink(log);
  return status;
}

}
