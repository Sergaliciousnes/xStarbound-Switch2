#include "StarRenderer_vulkan.hpp"
#include "StarSwitch2ExplorationProfile.hpp"
#include "StarSwitch2WaitProfile.hpp"
#include "StarTextureAtlas.hpp"
#include "StarSwitch2Diagnostics.hpp"
#include "StarRenderProfiling.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <cstring>

namespace Star {
namespace {

uint64_t nextTextureId() {
  static std::atomic<uint64_t> nextId{1};
  return nextId.fetch_add(1);
}

VulkanTextureDataPtr makeTextureData(Image const& image, TextureAddressing addressing, TextureFiltering filtering, bool lighting = false) {
  STAR_EXPLORATION_SCOPE(trace,Switch2ExplorationProfile::Kind::TextureCreate,"standalone",uint64_t(image.width())*image.height()*4);
  Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Texture, "standalone texture: convert image");
  STAR_RENDER_ZONE("Vulkan CPU texture snapshot");
  STAR_RENDER_TEXT("%ux%u", image.width(), image.height());
  auto source = make_shared<VulkanTextureData>(VulkanTextureData{nextTextureId(), image.convert(PixelFormat::RGBA32), addressing, filtering});
  source->lighting = lighting;
  Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Texture, "standalone texture: complete");
  return source;
}

class VulkanTextureBase : public Texture {
public:
  virtual VulkanTextureDataPtr source() const = 0;
  virtual Vec2U offset() const { return {}; }
};

class VulkanTexture : public VulkanTextureBase {
public:
  explicit VulkanTexture(VulkanTextureDataPtr source) : data(std::move(source)) {}
  Vec2U size() const override { return data->size(); }
  TextureFiltering filtering() const override { return data->filtering; }
  TextureAddressing addressing() const override { return data->addressing; }
  VulkanTextureDataPtr source() const override { return data; }
  VulkanTextureDataPtr data;
};

struct VulkanAtlasPage {
  Vec2U size;
  TextureFiltering filtering;
  std::vector<shared_ptr<Image>> tiles;
  shared_ptr<uint8_t const> owner = make_shared<uint8_t>(0);
  VulkanTextureDataPtr snapshot;

  VulkanAtlasPage(Vec2U pageSize, TextureFiltering filter) : size(pageSize), filtering(filter) {
    constexpr unsigned tileSize = VulkanTextureData::AtlasTileSize;
    if (!size[0] || !size[1] || size[0] % tileSize || size[1] % tileSize)
      throw RendererException("Vulkan atlas dimensions must be multiples of the tile size");
    auto blank = make_shared<Image>(Image::filled({tileSize, tileSize}, {0, 0, 0, 0}));
    tiles.assign((size[0] / tileSize) * (size[1] / tileSize), blank);
  }

  VulkanTextureDataPtr source() {
    if (!snapshot) {
      STAR_EXPLORATION_SCOPE(trace,Switch2ExplorationProfile::Kind::AtlasSnapshot,"",tiles.size());
      STAR_RENDER_ZONE("Vulkan atlas tile-version snapshot");
      STAR_RENDER_COUNT(atlasSnapshots, 1);
      auto version = make_shared<VulkanTextureData>();
      version->id = nextTextureId();
      version->addressing = TextureAddressing::Clamp;
      version->filtering = filtering;
      version->atlasSize = size;
      version->atlasOwner = owner;
      version->atlasTiles.assign(tiles.begin(), tiles.end());
      snapshot = std::move(version);
    }
    return snapshot;
  }
};

class VulkanAtlasSet : public TextureAtlasSet<shared_ptr<VulkanAtlasPage>> {
public:
  VulkanAtlasSet(unsigned cells, TextureFiltering filter) : TextureAtlasSet(16, cells), filtering(filter) {}
  shared_ptr<VulkanAtlasPage> createAtlasTexture(Vec2U const& size, PixelFormat) override {
    return make_shared<VulkanAtlasPage>(size, filtering);
  }
  void destroyAtlasTexture(shared_ptr<VulkanAtlasPage> const&) override {}
  void copyAtlasPixels(shared_ptr<VulkanAtlasPage> const& page, Vec2U const& offset, Image const& image) override {
    STAR_RENDER_ZONE("Vulkan atlas region write");
    if (image.empty() || offset[0] > page->size[0] || offset[1] > page->size[1]
        || image.width() > page->size[0] - offset[0] || image.height() > page->size[1] - offset[1])
      throw RendererException("Vulkan atlas write outside page bounds");
    STAR_RENDER_COUNT(atlasWriteBytes, (uint64_t)image.width() * image.height() * 4);
    constexpr unsigned tileSize = VulkanTextureData::AtlasTileSize;
    Image converted;
    Image const* rgba = &image;
    if (image.pixelFormat() != PixelFormat::RGBA32) {
      converted = image.convert(PixelFormat::RGBA32);
      rgba = &converted;
    }
    for (unsigned ty = offset[1] / tileSize; ty <= (offset[1] + image.height() - 1) / tileSize; ++ty) {
      for (unsigned tx = offset[0] / tileSize; tx <= (offset[0] + image.width() - 1) / tileSize; ++tx) {
        auto& tile = page->tiles[ty * (page->size[0] / tileSize) + tx];
        if (!tile.unique()) {
          STAR_RENDER_ZONE("Vulkan atlas tile copy-on-write");
          tile = make_shared<Image>(*tile);
          STAR_RENDER_COUNT(atlasSnapshotBytes, (uint64_t)tileSize * tileSize * 4);
        }
        unsigned x0 = std::max(offset[0], tx * tileSize);
        unsigned x1 = std::min(offset[0] + image.width(), (tx + 1) * tileSize);
        unsigned y0 = std::max(offset[1], ty * tileSize);
        unsigned y1 = std::min(offset[1] + image.height(), (ty + 1) * tileSize);
        for (unsigned y = y0; y < y1; ++y)
          std::memcpy(tile->data() + ((y - ty * tileSize) * tileSize + x0 - tx * tileSize) * 4,
              rgba->data() + ((y - offset[1]) * image.width() + x0 - offset[0]) * 4, (x1 - x0) * 4);
      }
    }
    // Previously queued draws retain their immutable pixels and GPU identity.
    page->snapshot.reset();
  }
  TextureFiltering filtering;
};

class VulkanTextureGroup;
class VulkanGroupedTexture : public VulkanTextureBase {
public:
  ~VulkanGroupedTexture();
  Vec2U size() const override { return placement->imageSize(); }
  TextureFiltering filtering() const override { return placement->atlasTexture()->filtering; }
  TextureAddressing addressing() const override { return TextureAddressing::Clamp; }
  VulkanTextureDataPtr source() const override { return placement->atlasTexture()->source(); }
  Vec2U offset() const override { return placement->atlasTextureCoordinates().min(); }
  shared_ptr<VulkanTextureGroup> parent;
  VulkanAtlasSet::TextureHandle placement;
};

class VulkanTextureGroup : public TextureGroup, public enable_shared_from_this<VulkanTextureGroup> {
public:
  VulkanTextureGroup(unsigned cells, TextureFiltering filter) : atlas(cells, filter) {}
  ~VulkanTextureGroup() override { atlas.reset(); }
  TextureFiltering filtering() const override { return atlas.filtering; }
  TexturePtr create(Image const& image) override {
    STAR_EXPLORATION_SCOPE(trace,Switch2ExplorationProfile::Kind::TextureCreate,"atlas",uint64_t(image.width())*image.height()*4);
    STAR_EXPLORATION_COUNT(Switch2ExplorationProfile::Counter::TextureBytes,uint64_t(image.width())*image.height()*4);
    if (image.empty()) throw RendererException("Cannot create an empty Vulkan atlas texture");
    auto pageSize = atlas.atlasTextureSize();
    if (image.width() + 2 > pageSize[0] || image.height() + 2 > pageSize[1])
      return make_ref<VulkanTexture>(makeTextureData(image, TextureAddressing::Clamp, filtering()));
    auto texture = make_ref<VulkanGroupedTexture>();
    texture->parent = shared_from_this();
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Texture, "atlas texture: border/placement/copy");
    texture->placement = atlas.addTexture(image);
    Switch2Diagnostics::stage(Switch2Diagnostics::Channel::Texture, "atlas texture: complete");
    return texture;
  }
  VulkanAtlasSet atlas;
};

VulkanGroupedTexture::~VulkanGroupedTexture() {
  if (placement) parent->atlas.freeTexture(placement);
}

class VulkanRenderBuffer : public RenderBuffer {
public:
  void set(List<RenderPrimitive>& source) override {
#ifdef TRACY_ENABLE
    ++generation;
#endif
    primitives = source;
    prepared = false;
    vertices.clear(); draws.clear(); bindings.clear();
  }
#ifdef TRACY_ENABLE
  inline static std::atomic<uint64_t> nextId{1};
  uint64_t id = nextId.fetch_add(1);
  uint64_t generation = 0;
#endif
  List<RenderPrimitive> primitives;
  bool prepared = false;
  bool multiTexturing = false;
  std::vector<VulkanRenderer::Vertex> vertices;
  std::vector<VulkanRenderer::Draw> draws;
  std::vector<std::vector<TexturePtr>> bindings;
};

VulkanTextureDataPtr textureData(TexturePtr const& texture) {
  auto vkTexture = dynamic_cast<VulkanTextureBase const*>(texture.get());
  if (!vkTexture) throw RendererException("Texture does not belong to the Vulkan backend");
  return vkTexture->source();
}

}

VulkanRenderer::VulkanRenderer(bool gameplay) : m_gameplay(gameplay) {
  m_white = textureData(createTexture(Image::filled({1, 1}, {255, 255, 255, 255}), TextureAddressing::Clamp, TextureFiltering::Nearest));
  m_worldState.lightMap = m_white;
  m_transparent = makeTextureData(Image::filled({1, 1}, {0, 0, 0, 0}), TextureAddressing::Clamp, TextureFiltering::Linear);
  m_worldState.tileLightMap = m_transparent;
}

String VulkanRenderer::rendererId() const { return "Vulkan"; }
bool VulkanRenderer::supportsWorldRendering() const { return m_gameplay; }
bool VulkanRenderer::worldEnabled() const { return m_worldLoaded; }
Vec2U VulkanRenderer::screenSize() const { return m_size; }
void VulkanRenderer::setScreenSize(Vec2U size) { flush(); m_size = size; }
void VulkanRenderer::startFrame() {
  STAR_RENDER_RESET();
  m_immediate.clear(); m_vertices.clear(); m_draws.clear(); m_worldComposited = false;
}
void VulkanRenderer::finishFrame() {
  flush();
  mergeAdjacentDraws();
#ifdef TRACY_ENABLE
  size_t totalAtlasPages = 0;
  size_t totalAtlasTextures = 0;
  float totalFillLevel = 0.0f;
  size_t groupCount = 0;
  for (auto it = m_liveTextureGroups.begin(); it != m_liveTextureGroups.end(); ) {
    if (auto group = it->lock()) {
      if (auto vkGroup = dynamic_cast<VulkanTextureGroup*>(group.get())) {
        totalAtlasPages += vkGroup->atlas.totalAtlases();
        totalAtlasTextures += vkGroup->atlas.totalTextures();
        totalFillLevel += vkGroup->atlas.averageFillLevel();
        ++groupCount;
      }
      ++it;
    } else {
      it = m_liveTextureGroups.erase(it);
    }
  }
  STAR_RENDER_PLOT("Vulkan active atlas pages", totalAtlasPages);
  STAR_RENDER_PLOT("Vulkan active atlas textures", totalAtlasTextures);
  STAR_RENDER_PLOT("Vulkan average atlas fill %", groupCount ? (totalFillLevel / groupCount * 100.0f) : 0.0f);
#endif
}
void VulkanRenderer::loadConfig(Json const& config) {
  for (auto const& pair : config.getObject("frameBuffers", {}))
    if (pair.first != "world") throw RendererException("Only the Vulkan world render target is supported");
}
void VulkanRenderer::loadEffectConfig(String const& name, Json const& config, StringMap<String> const& shaders) {
  if ((name != "interface" && name != "world") || config.getString("builtin", "") != name || !shaders.empty())
    throw RendererException("Vulkan supports only the built-in interface and world effects");
  flush();
  if (name == "interface") m_interfaceLoaded = true;
  else m_worldLoaded = true;
}
void VulkanRenderer::setEffectParameter(String const& name, RenderEffectParameter const& value) {
  if (!m_currentWorld) return;
  auto set = [&](auto& destination) {
    using T = std::decay_t<decltype(destination)>;
    auto typed = value.ptr<T>();
    if (!typed) throw RendererException::format("Vulkan effect parameter '{}' type mismatch", name);
    if (destination != *typed) { flush(); destination = *typed; }
  };
  if (name == "lightMapEnabled") set(m_worldState.lightMapEnabled);
  else if (name == "lightMapScale") {
    auto scale = value.ptr<Vec2F>();
    if (scale && ((*scale)[0] == 0 || (*scale)[1] == 0)) throw RendererException("Lightmap scale must be nonzero");
    set(m_worldState.lightMapScale);
  } else if (name == "lightMapOffset") set(m_worldState.lightMapOffset);
  else if (name == "lightMapMultiplier") set(m_worldState.lightMapMultiplier);
  else for (size_t i = 0; i < 6; ++i)
    if (name == strf("param{}", i + 1)) { set(m_worldState.parameters[i]); break; }
}
void VulkanRenderer::setEffectTexture(String const& name, Image const& image) {
  if (!m_currentWorld || (name != "lightMap" && name != "tileLightMap")) return;
  flush();
  // WorldPainter can render before asynchronous lighting has produced maps.
  // Keep descriptors valid and avoid zero texture dimensions during startup.
  auto source = image.empty() ? (name == "lightMap" ? m_white : m_transparent)
      : makeTextureData(image, TextureAddressing::Clamp, TextureFiltering::Linear, true);
  if (name == "lightMap") m_worldState.lightMap = std::move(source);
  else m_worldState.tileLightMap = std::move(source);
}
bool VulkanRenderer::switchEffectConfig(String const& name) {
  flush();
  if (name == "world" && m_worldLoaded) { m_currentWorld = true; return true; }
  if (name != "internal" && !(name == "interface" && m_interfaceLoaded)) return false;
  if (name == "interface" && m_currentWorld && !m_worldComposited) {
    Draw copy{(uint32_t)m_vertices.size(), 6, Mat3F::identity(), {}, {m_white}};
    copy.effect = m_worldState;
    copy.composite = true;
    float w = (float)m_size[0], h = (float)m_size[1];
    Vertex a{{{0, 0}, {0, 1}, {255, 255, 255, 255}, 0}, 0};
    Vertex b{{{w, 0}, {1, 1}, {255, 255, 255, 255}, 0}, 0};
    Vertex c{{{w, h}, {1, 0}, {255, 255, 255, 255}, 0}, 0};
    Vertex d{{{0, h}, {0, 0}, {255, 255, 255, 255}, 0}, 0};
    m_vertices.insert(m_vertices.end(), {a, b, c, a, c, d});
    m_draws.push_back(std::move(copy));
    m_worldComposited = true;
  }
  m_currentWorld = false;
  return true;
}
TexturePtr VulkanRenderer::createTexture(Image const& image, TextureAddressing addressing, TextureFiltering filtering) {
  if (image.empty()) throw RendererException("Cannot create an empty Vulkan texture");
  return make_ref<VulkanTexture>(makeTextureData(image, addressing, filtering));
}
TextureGroupPtr VulkanRenderer::createTextureGroup(TextureGroupSize size, TextureFiltering filtering) {
  if (m_limitTextureGroupSize && size == TextureGroupSize::Large) size = TextureGroupSize::Medium;
  // Conservative pages limit the temporary memory cost of immutable snapshots.
  unsigned cells = size == TextureGroupSize::Small ? 16 : size == TextureGroupSize::Medium ? 32 : 64;
  auto group = make_shared<VulkanTextureGroup>(cells, filtering);
#ifdef TRACY_ENABLE
  m_liveTextureGroups.push_back(group);
#endif
  return group;
}
void VulkanRenderer::setSizeLimitEnabled(bool enabled) { m_limitTextureGroupSize = enabled; }
void VulkanRenderer::setMultiTexturingEnabled(bool enabled) { flush(); m_multiTexturing = enabled; }
RenderBufferPtr VulkanRenderer::createRenderBuffer() { return make_shared<VulkanRenderBuffer>(); }
List<RenderPrimitive>& VulkanRenderer::immediatePrimitives() { return m_immediate; }
void VulkanRenderer::render(RenderPrimitive primitive) { m_immediate.append(std::move(primitive)); }

void VulkanRenderer::setScissorRect(Maybe<RectI> const& rect) {
  if (rect == m_scissor) return;
  flush();
  m_scissor = rect;
}

void VulkanRenderer::renderBuffer(RenderBufferPtr const& buffer, Mat3F const& transformation) {
  Switch2WaitProfile::Scope cachedScope(Switch2WaitProfile::Metric::RendererCached);
  Switch2WaitProfile::count(Switch2WaitProfile::Counter::CachedBuffers);
  flush();
  auto vkBuffer = dynamic_cast<VulkanRenderBuffer*>(buffer.get());
  if (!vkBuffer) throw RendererException("Render buffer does not belong to the Vulkan backend");
  if (!vkBuffer->prepared || vkBuffer->multiTexturing != m_multiTexturing) {
    Switch2WaitProfile::count(Switch2WaitProfile::Counter::CachedRebuilds);
    STAR_RENDER_ZONE("Vulkan rebuild cached geometry");
    auto firstVertex = m_vertices.size();
    auto firstDraw = m_draws.size();
    vkBuffer->prepared = false;
    vkBuffer->bindings.clear();
    append(vkBuffer->primitives, transformation, &vkBuffer->bindings);
    vkBuffer->vertices.assign(m_vertices.begin() + firstVertex, m_vertices.end());
    vkBuffer->draws.assign(m_draws.begin() + firstDraw, m_draws.end());
    Switch2WaitProfile::count(Switch2WaitProfile::Counter::CachedVertices,vkBuffer->vertices.size());
    Switch2WaitProfile::count(Switch2WaitProfile::Counter::CachedDraws,vkBuffer->draws.size());
    for (auto& draw : vkBuffer->draws) {
      draw.firstVertex -= (uint32_t)firstVertex;
      // Cache topology and live handles, never immutable atlas versions or draw state.
      draw.textures.clear();
      draw.effect = {};
    }
    STAR_RENDER_TEXT("buffer=%llu generation=%llu vertices=%zu batches=%zu texture-limit-splits=%zu slots=%u",
        (unsigned long long)vkBuffer->id, (unsigned long long)vkBuffer->generation,
        vkBuffer->vertices.size(), vkBuffer->draws.size(), vkBuffer->draws.empty() ? 0 : vkBuffer->draws.size() - 1,
        m_multiTexturing ? 4u : 1u);
    STAR_RENDER_COUNT(cachedPreparedBatches, vkBuffer->draws.size());
    vkBuffer->multiTexturing = m_multiTexturing;
    vkBuffer->prepared = true;
    return;
  }
  // Live atlas placements/dimensions are stable: relocation is disabled.
  // Refresh page versions below while reusing the normalized vertex UVs.
  STAR_RENDER_ZONE("Vulkan reuse cached geometry");
  if (vkBuffer->vertices.size() > std::numeric_limits<uint32_t>::max() - m_vertices.size())
    throw RendererException("Vulkan frame exceeds the vertex index limit");
  auto firstVertex = (uint32_t)m_vertices.size();
  Switch2WaitProfile::count(Switch2WaitProfile::Counter::CachedVertices,vkBuffer->vertices.size());
  Switch2WaitProfile::count(Switch2WaitProfile::Counter::CachedDraws,vkBuffer->draws.size());
  m_vertices.insert(m_vertices.end(), vkBuffer->vertices.begin(), vkBuffer->vertices.end());
  STAR_RENDER_COUNT(cachedReplayBatches, vkBuffer->draws.size());
  STAR_RENDER_TEXT("buffer=%llu generation=%llu vertices=%zu batches=%zu texture-limit-splits=%zu slots=%u",
      (unsigned long long)vkBuffer->id, (unsigned long long)vkBuffer->generation,
      vkBuffer->vertices.size(), vkBuffer->draws.size(), vkBuffer->draws.empty() ? 0 : vkBuffer->draws.size() - 1,
      m_multiTexturing ? 4u : 1u);
  for (size_t i = 0; i < vkBuffer->draws.size(); ++i) {
    auto draw = vkBuffer->draws[i];
    draw.firstVertex += firstVertex;
    draw.transform = transformation;
    draw.scissor = m_scissor;
    draw.effect = m_worldState;
    draw.world = m_currentWorld;
    for (auto const& texture : vkBuffer->bindings[i])
      draw.textures.push_back(texture ? textureData(texture) : m_white);
    m_draws.push_back(std::move(draw));
  }
}

void VulkanRenderer::append(List<RenderPrimitive> const& primitives, Mat3F const& transform,
    std::vector<std::vector<TexturePtr>>* bindings) {
  STAR_RENDER_ZONE("Vulkan prepare draw batches");
  Draw batch{(uint32_t)m_vertices.size(), 0, transform, m_scissor, {}};
  batch.world = m_currentWorld;
  batch.effect = m_worldState;
  std::vector<TexturePtr> batchBindings;
  auto finish = [&]() {
    if (batch.vertexCount) {
      m_draws.push_back(std::move(batch));
      if (bindings) bindings->push_back(std::move(batchBindings));
    }
    batchBindings.clear();
    batch = {(uint32_t)m_vertices.size(), 0, transform, m_scissor, {}};
    batch.world = m_currentWorld;
    batch.effect = m_worldState;
  };
  Vec2F textureOffset{};
  Vec2F invTextureSize{};
  auto slot = [&](TexturePtr const& texture) {
    textureOffset = {};
    VulkanTextureDataPtr data;
    if (texture) {
      auto vkTexture = dynamic_cast<VulkanTextureBase const*>(texture.get());
      if (!vkTexture) throw RendererException("Texture does not belong to the Vulkan backend");
      textureOffset = Vec2F(vkTexture->offset());
      data = vkTexture->source();
    } else {
      data = m_white;
    }
    uint32_t index;
    auto found = std::find(batch.textures.begin(), batch.textures.end(), data);
    if (found == batch.textures.end()) {
      if (batch.textures.size() == (m_multiTexturing ? 4u : 1u)) {
        STAR_RENDER_COUNT(batchTextureLimitSplits, 1);
        finish();
      }
      batch.textures.push_back(std::move(data));
      if (bindings) batchBindings.push_back(texture);
      index = (uint32_t)batch.textures.size() - 1;
    } else {
      index = (uint32_t)(found - batch.textures.begin());
    }
    auto size = batch.textures[index]->size();
    invTextureSize = {1.0f / size[0], 1.0f / size[1]};
    return index;
  };
  auto vertex = [&](RenderVertex v, uint32_t index) {
    if (m_vertices.size() == std::numeric_limits<uint32_t>::max())
      throw RendererException("Vulkan frame exceeds the vertex index limit");
    v.textureCoordinate[0] = (v.textureCoordinate[0] + textureOffset[0]) * invTextureSize[0];
    v.textureCoordinate[1] = (v.textureCoordinate[1] + textureOffset[1]) * invTextureSize[1];
    m_vertices.push_back({v, index});
    ++batch.vertexCount;
  };
  for (auto const& primitive : primitives) {
    if (auto triangle = primitive.ptr<RenderTriangle>()) {
      auto index = slot(triangle->texture);
      vertex(triangle->a, index); vertex(triangle->b, index); vertex(triangle->c, index);
    } else if (auto quad = primitive.ptr<RenderQuad>()) {
      auto index = slot(quad->texture);
      vertex(quad->a, index); vertex(quad->b, index); vertex(quad->c, index);
      vertex(quad->a, index); vertex(quad->c, index); vertex(quad->d, index);
    } else if (auto poly = primitive.ptr<RenderPoly>()) {
      if (poly->vertexes.size() < 3) continue;
      auto index = slot(poly->texture);
      for (size_t i = 1; i + 1 < poly->vertexes.size(); ++i) {
        vertex(poly->vertexes[0], index); vertex(poly->vertexes[i], index); vertex(poly->vertexes[i + 1], index);
      }
    }
  }
  finish();
}

void VulkanRenderer::mergeAdjacentDraws() {
  STAR_RENDER_ZONE("Vulkan merge adjacent draws");
  // Finalize only after cached topology has been captured. Never reorder or
  // remap vertices: equal ordered texture slots preserve their texture indices.
  size_t output = 0;
  for (size_t i = 0; i < m_draws.size(); ++i) {
    auto& draw = m_draws[i];
    if (output) {
      auto& previous = m_draws[output - 1];
      bool contiguous = (uint64_t)previous.firstVertex + previous.vertexCount == draw.firstVertex;
      bool state = !previous.composite && !draw.composite && previous.world == draw.world
          && previous.transform == draw.transform && previous.scissor == draw.scissor
          && previous.effect == draw.effect;
      if (contiguous && state && previous.textures == draw.textures
          && draw.vertexCount <= UINT32_MAX - previous.vertexCount) {
        previous.vertexCount += draw.vertexCount;
        STAR_RENDER_COUNT(mergedDraws, 1);
        continue;
      }
      if (!contiguous) STAR_RENDER_COUNT(mergeRangeBreaks, 1);
      else if (!state) STAR_RENDER_COUNT(mergeStateBreaks, 1);
      else STAR_RENDER_COUNT(mergeTextureBreaks, 1);
    }
    if (output != i) m_draws[output] = std::move(draw);
    ++output;
  }
  m_draws.resize(output);
}

void VulkanRenderer::flush() {
  if (m_immediate.empty()) return;
  Switch2WaitProfile::Scope immediateScope(Switch2WaitProfile::Metric::RendererImmediate);
  Switch2WaitProfile::count(Switch2WaitProfile::Counter::ImmediatePrimitives,m_immediate.size());
  STAR_RENDER_ZONE("Vulkan prepare immediate geometry");
  auto prevDraws = m_draws.size();
  auto prevVertices=m_vertices.size();
  append(m_immediate, Mat3F::identity());
  Switch2WaitProfile::count(Switch2WaitProfile::Counter::ImmediateVertices,m_vertices.size()-prevVertices);
  Switch2WaitProfile::count(Switch2WaitProfile::Counter::ImmediateDraws,m_draws.size()-prevDraws);
  STAR_RENDER_COUNT(immediateBatches, m_draws.size() - prevDraws);
  m_immediate.clear();
}

std::vector<VulkanRenderer::Vertex> const& VulkanRenderer::vertices() const { return m_vertices; }
std::vector<VulkanRenderer::Draw> const& VulkanRenderer::draws() const { return m_draws; }
VulkanTextureDataPtr const& VulkanRenderer::whiteTexture() const { return m_white; }

}
