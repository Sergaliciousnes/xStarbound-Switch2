#include "StarAssetTextureGroup.hpp"
#include "StarArmorWearer.hpp"
#include <cstring>
#include "StarRoot.hpp"
#include "StarRootLoader.hpp"
#include <cstdio>
#include <stdexcept>
#ifdef STAR_TEST_VULKAN
#include "StarEnvironmentPainter.hpp"
#include "StarRenderer_vulkan.hpp"
#include <cstring>
#endif
#ifdef STAR_USE_RPMALLOC
#include "rpmalloc.h"
#endif
// Keep allocator-owned test objects in this executable (the game overrides new).
#define CACHE_CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)
#define TEST(group, name) void test##name()
#define ASSERT_TRUE(expr) CACHE_CHECK(expr)
#define EXPECT_TRUE(expr) CACHE_CHECK(expr)
#define EXPECT_FALSE(expr) CACHE_CHECK(!(expr))
#define EXPECT_EQ(a, b) CACHE_CHECK((a) == (b))
#include <chrono>
#include <thread>

using namespace Star;
namespace {
class CacheTestTexture : public Texture {
public:
  Vec2U size() const override { return {2, 2}; }
  TextureFiltering filtering() const override { return TextureFiltering::Nearest; }
  TextureAddressing addressing() const override { return TextureAddressing::Clamp; }
};
class CacheTestGroup : public TextureGroup {
public:
  TextureFiltering filtering() const override { return TextureFiltering::Nearest; }
  TexturePtr create(Image const&) override { return make_ref<CacheTestTexture>(); }
};
// Real asset loading supplies distinct image identities; textures have a small
// fixed payload so budget boundary cases require no GPU or large allocations.
const char* const A = "/parallax/images/clouds1/base/1.png";
const char* const B = "/parallax/images/clouds1/base/2.png";
const char* const C = "/parallax/images/clouds2/base/1.png";
void nextTimestamp() { std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
}

TEST(AssetTextureCache, IdleBudgetReplacesTimeout) {
  AssetTextureGroup cache(make_shared<CacheTestGroup>(), 16);
  auto texture = cache.loadTexture(A);
  ASSERT_TRUE(texture);
  cache.cleanup(0); // Working set.
  cache.cleanup(0); // Idle, retained despite zero timeout.
  EXPECT_TRUE(cache.textureLoaded(A));
  EXPECT_EQ(cache.loadTexture(A).get(), texture.get());
}

TEST(AssetTextureCache, ProtectsWorkingSetAndBoundsIdleRetention) {
  AssetTextureGroup cache(make_shared<CacheTestGroup>(), 16);
  ASSERT_TRUE(cache.loadTexture(A));
  ASSERT_TRUE(cache.loadTexture(B));
  ASSERT_TRUE(cache.loadTexture(C));
  cache.cleanup(0);
  EXPECT_TRUE(cache.textureLoaded(A) && cache.textureLoaded(B) && cache.textureLoaded(C));
  cache.cleanup(0);
  EXPECT_EQ((int)cache.textureLoaded(A) + (int)cache.textureLoaded(B) + (int)cache.textureLoaded(C), 1);
}

TEST(AssetTextureCache, EvictsLeastRecentlyUsedIdleTexture) {
  AssetTextureGroup cache(make_shared<CacheTestGroup>(), 16);
  auto old = cache.loadTexture(A);
  ASSERT_TRUE(old);
  cache.cleanup(0);
  nextTimestamp();
  ASSERT_TRUE(cache.loadTexture(B));
  cache.cleanup(0);
  nextTimestamp();
  ASSERT_TRUE(cache.loadTexture(A));
  cache.cleanup(0);
  nextTimestamp();
  ASSERT_TRUE(cache.loadTexture(C));
  cache.cleanup(0);
  EXPECT_TRUE(cache.textureLoaded(A));
  EXPECT_FALSE(cache.textureLoaded(B));
  EXPECT_TRUE(cache.textureLoaded(C));
  // External owners remain valid after cache eviction.
  EXPECT_EQ(old->size(), Vec2U(2, 2));
}

TEST(AssetTextureCache, DefaultCacheStillExpires) {
  AssetTextureGroup cache(make_shared<CacheTestGroup>());
  ASSERT_TRUE(cache.loadTexture(A));
  cache.cleanup(0);
  EXPECT_FALSE(cache.textureLoaded(A));
}

TEST(AssetTextureCache, ReloadInvalidatesRetainedTextures) {
  AssetTextureGroup cache(make_shared<CacheTestGroup>(), 16);
  ASSERT_TRUE(cache.loadTexture(A));
  cache.cleanup(0);
  Root::singleton().reload();
  cache.cleanup(0);
  EXPECT_FALSE(cache.textureLoaded(A));
}

#ifdef STAR_TEST_VULKAN
void testCachedGeometryRefresh() {
  auto renderer = make_shared<VulkanRenderer>();
  auto group = renderer->createTextureGroup(TextureGroupSize::Large, TextureFiltering::Nearest);
  auto texture = group->create(Image::filled({8, 8}, {255, 0, 0, 255}));
  auto buffer = renderer->createRenderBuffer();
  List<RenderPrimitive> primitives;
  primitives.append(RenderQuad(texture, RectF({0, 0}, {8, 8})));
  buffer->set(primitives);
  renderer->startFrame();
  renderer->renderBuffer(buffer, Mat3F::identity());
  auto old = renderer->draws().back().textures.front();
  auto vertices = renderer->vertices();
  // Mutate the same atlas page while keeping the cached texture alive.
  auto other = group->create(Image::filled({8, 8}, {0, 255, 0, 255}));
  renderer->renderBuffer(buffer, Mat3F::identity());
  auto current = renderer->draws().back().textures.front();
  CACHE_CHECK(old->atlasOwner == current->atlasOwner);
  CACHE_CHECK(old->id != current->id);
  CACHE_CHECK(renderer->draws().front().textures.front()->id == old->id);
  CACHE_CHECK(renderer->vertices().size() == vertices.size() * 2);
  for (size_t i = 0; i < vertices.size(); ++i) {
    CACHE_CHECK(renderer->vertices()[i + vertices.size()].vertex.textureCoordinate == vertices[i].vertex.textureCoordinate);
    CACHE_CHECK(renderer->vertices()[i + vertices.size()].textureIndex == vertices[i].textureIndex);
  }
  renderer->startFrame();
  renderer->setScissorRect(RectI({1, 2}, {20, 30}));
  renderer->renderBuffer(buffer, Mat3F::identity());
  CACHE_CHECK(renderer->draws().back().scissor == Maybe<RectI>(RectI({1, 2}, {20, 30})));
  CACHE_CHECK(renderer->draws().back().textures.front()->id == current->id);
  primitives.clear();
  buffer->set(primitives);
  renderer->startFrame();
  renderer->renderBuffer(buffer, Mat3F::identity());
  CACHE_CHECK(renderer->vertices().empty());
  CACHE_CHECK(renderer->draws().empty());
}

void testAtlasTileVersionIsolation() {
  auto renderer = make_shared<VulkanRenderer>();
  renderer->setScreenSize({640, 360});
  auto group = renderer->createTextureGroup(TextureGroupSize::Large, TextureFiltering::Nearest);
  renderer->startFrame();
  auto first = group->create(Image::filled({70, 70}, {255, 0, 0, 255}));
  renderer->render(RenderQuad(first, RectF({0, 0}, {70, 70})));
  renderer->flush();
  auto old = renderer->draws().back().textures.front();
  first.reset();
  auto second = group->create(Image::filled({70, 70}, {0, 255, 0, 255}));
  renderer->render(RenderQuad(second, RectF({100, 0}, {170, 70})));
  renderer->flush();
  auto current = renderer->draws().back().textures.front();
  CACHE_CHECK(old->atlasOwner == current->atlasOwner);
  CACHE_CHECK(old->id != current->id);
  CACHE_CHECK(old->atlasTiles[0]->get32(1, 1) == Vec4B(255, 0, 0, 255));
  CACHE_CHECK(old->atlasTiles[1]->get32(5, 5) == Vec4B(255, 0, 0, 255));
  CACHE_CHECK(current->atlasTiles[0]->get32(1, 1) == Vec4B(0, 255, 0, 255));
  CACHE_CHECK(current->atlasTiles[1]->get32(5, 5) == Vec4B(0, 255, 0, 255));
  size_t changed = 0;
  for (size_t i = 0; i < old->atlasTiles.size(); ++i)
    if (old->atlasTiles[i] != current->atlasTiles[i]) ++changed;
  renderer->finishFrame();
  CACHE_CHECK(renderer->draws().size() == 2); // Distinct immutable versions must not merge.
  CACHE_CHECK(changed == 4); // 72x72 with border crosses four 64px tiles.
  CACHE_CHECK(current->pixels.empty()); // No hidden whole-page CPU copy.
}

void testParallaxStandaloneReuse() {
  auto renderer = make_shared<VulkanRenderer>();
  renderer->setScreenSize({640, 360});
  EnvironmentPainter painter(renderer);
  WorldCamera camera;
  camera.setScreenSize({640, 360});
  SkyRenderData sky{};
  sky.dayLength = 100;
  ParallaxLayer layer;
  layer.parallaxValue = {1, 1};
  layer.parallaxOffset = {};
  layer.repeat = {true, true};
  layer.unlit = true;
  auto render = [&](char const* path) {
    layer.textures = {path};
    renderer->startFrame();
    painter.renderParallaxLayers({}, camera, {layer}, sky);
    renderer->finishFrame();
    CACHE_CHECK(!renderer->draws().empty());
    auto source = renderer->draws().front().textures.front();
    painter.cleanup(0);
    return source;
  };
  auto first = render(A);
  auto expected = Root::singleton().assets()->image(A)->convert(PixelFormat::RGBA32);
  CACHE_CHECK(first->pixels.size() == expected.size());
  CACHE_CHECK(std::memcmp(first->pixels.data(), expected.data(), (size_t)expected.width() * expected.height() * 4) == 0);
  render(B);
  render(C);
  CACHE_CHECK(render(A)->id == first->id);
}
#endif

void testIdentityTagInvalidation() {
  HumanoidIdentity identity;
  HumanoidIdentityTagCache cache;
  auto original = cache.get(identity);
  CACHE_CHECK(original.size() == 21);
  CACHE_CHECK(cache.get(identity) == original);
  identity.name = "Name is not a substitution input";
  CACHE_CHECK(cache.get(identity) == original);
  // Mutate one dependency at a time and compare with a freshly built map.
  auto check = [&](auto mutate, char const* tag) {
    auto before = cache.get(identity);
    mutate();
    HumanoidIdentityTagCache fresh;
    CACHE_CHECK(cache.get(identity) == fresh.get(identity));
    CACHE_CHECK(cache.get(identity).get(tag) != before.get(tag));
    CACHE_CHECK(cache.get(identity) == fresh.get(identity));
  };
  check([&] { identity.species = "avian"; }, "species");
  check([&] { identity.imagePath = String("custom"); }, "imagePath");
  check([&] { identity.gender = Gender::Female; }, "gender");
  check([&] { identity.hairGroup = "changed"; }, "hairGroup");
  check([&] { identity.hairType = "changed"; }, "hairType");
  check([&] { identity.facialHairGroup = "changed"; }, "facialHairGroup");
  check([&] { identity.facialHairType = "changed"; }, "facialHairType");
  check([&] { identity.facialMaskGroup = "changed"; }, "facialMaskGroup");
  check([&] { identity.facialMaskType = "changed"; }, "facialMaskType");
  check([&] { identity.bodyDirectives = String("?multiply=ff0000"); }, "bodyDirectives");
  check([&] { identity.emoteDirectives = String("?multiply=ff0000"); }, "emoteDirectives");
  check([&] { identity.hairDirectives = String("?multiply=ff0000"); }, "hairDirectives");
  check([&] { identity.facialHairDirectives = String("?multiply=ff0000"); }, "facialHairDirectives");
  check([&] { identity.facialMaskDirectives = String("?multiply=ff0000"); }, "facialMaskDirectives");
  check([&] { identity.personality.idle = "idle.2"; }, "personalityIdle");
  check([&] { identity.personality.armIdle = "idle.3"; }, "personalityArmIdle");
  check([&] { identity.personality.headOffset[0] = 2; }, "headOffsetX");
  check([&] { identity.personality.headOffset[1] = 3; }, "headOffsetY");
  check([&] { identity.personality.armOffset[0] = 4; }, "armOffsetX");
  check([&] { identity.personality.armOffset[1] = 5; }, "armOffsetY");
  check([&] { identity.color = Vec4B(12, 34, 56, 78); }, "color");
  identity.personality.headOffset[0] = 0.0f;
  cache.get(identity);
  check([&] { identity.personality.headOffset[0] = -0.0f; }, "headOffsetX");
  identity.imagePath.reset();
  CACHE_CHECK(cache.get(identity).get("imagePath") == identity.species);
}

#ifdef STAR_TEST_VULKAN
void testAdjacentDrawMerging() {
  auto r = make_shared<VulkanRenderer>();
  r->setScreenSize({640, 360});
  auto texture = r->createTexture(Image::filled({8, 8}, {255, 0, 0, 128}), TextureAddressing::Clamp, TextureFiltering::Nearest);
  auto buffer = r->createRenderBuffer();
  List<RenderPrimitive> primitives;
  primitives.append(RenderQuad(texture, RectF({0, 0}, {8, 8})));
  buffer->set(primitives);
  auto replay = [&] { r->renderBuffer(buffer, Mat3F::identity()); };
  for (int frame = 0; frame < 2; ++frame) {
    r->startFrame();
    replay(); replay(); replay();
    auto vertices = r->vertices();
    CACHE_CHECK(r->draws().size() == 3);
    r->finishFrame();
    CACHE_CHECK(r->draws().size() == 1);
    CACHE_CHECK(r->draws()[0].vertexCount == 18);
    for (size_t i = 0; i < vertices.size(); ++i) {
      CACHE_CHECK(r->vertices()[i].vertex.screenCoordinate == vertices[i].vertex.screenCoordinate);
      CACHE_CHECK(r->vertices()[i].vertex.textureCoordinate == vertices[i].vertex.textureCoordinate);
      CACHE_CHECK(r->vertices()[i].vertex.color == vertices[i].vertex.color);
      CACHE_CHECK(r->vertices()[i].textureIndex == vertices[i].textureIndex);
    }
  }
  r->startFrame(); replay();
  r->renderBuffer(buffer, Mat3F::translation(Vec2F(10, 20)));
  r->setScissorRect(RectI({1, 2}, {20, 30})); replay();
  r->finishFrame(); CACHE_CHECK(r->draws().size() == 3);
  r->loadEffectConfig("world", JsonObject{{"builtin", "world"}}, {});
  r->loadEffectConfig("interface", JsonObject{{"builtin", "interface"}}, {});
  r->startFrame(); r->switchEffectConfig("world"); replay();
  r->setEffectParameter("lightMapMultiplier", 2.0f); replay();
  r->setEffectTexture("lightMap", Image::filled({2, 2}, {200, 200, 200, 255})); replay();
  r->switchEffectConfig("interface"); replay();
  r->finishFrame(); CACHE_CHECK(r->draws().size() == 5);
  CACHE_CHECK(r->draws()[3].composite);

  auto other = r->createTexture(Image::filled({8, 8}, {0, 255, 0, 128}), TextureAddressing::Clamp, TextureFiltering::Nearest);
  r->startFrame();
  r->render(RenderQuad(texture, RectF({0, 0}, {8, 8})));
  r->render(RenderQuad(other, RectF({0, 0}, {8, 8}))); r->flush();
  r->render(RenderQuad(other, RectF({0, 0}, {8, 8})));
  r->render(RenderQuad(texture, RectF({0, 0}, {8, 8})));
  r->finishFrame(); CACHE_CHECK(r->draws().size() == 2);
}
#endif

int main(int argc, char** argv) {
#ifdef STAR_USE_RPMALLOC
  ::rpmalloc_initialize(0);
#endif
  try {
    testIdentityTagInvalidation();
#ifdef STAR_TEST_VULKAN
    testAdjacentDrawMerging();
    testCachedGeometryRefresh();
    testAtlasTileVersionIsolation();
#endif
    if (argc == 2 && std::strcmp(argv[1], "--renderer-unit-tests") == 0) {
      std::puts("Passed identity invalidation and Vulkan geometry unit tests");
      return 0;
    }
    auto settings = RootLoader({{}, {}, {}, LogLevel::Error, true, {}}).commandParseOrDie(argc, argv).first;
    auto root = make_unique<Root>(settings);
    testIdleBudgetReplacesTimeout();
    testProtectsWorkingSetAndBoundsIdleRetention();
    testEvictsLeastRecentlyUsedIdleTexture();
    testDefaultCacheStillExpires();
#ifdef STAR_TEST_VULKAN
    testCachedGeometryRefresh();
    testAtlasTileVersionIsolation();
    testParallaxStandaloneReuse();
#endif
    testReloadInvalidatesRetainedTextures();
    std::puts("Passed cache integration tests (including standalone parallax reuse when Vulkan is enabled)");
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "Cache test failed: %s\n", e.what());
    return 1;
  }
}
