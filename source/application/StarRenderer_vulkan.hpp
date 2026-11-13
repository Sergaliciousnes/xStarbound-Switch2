#ifndef STAR_RENDERER_VULKAN_HPP
#define STAR_RENDERER_VULKAN_HPP

#include "StarRenderer.hpp"
#include "StarStaticVector.hpp"
#include <vector>
#include <array>

namespace Star {

// Immutable upload sources outlive Texture handles while queued draws use them.
struct VulkanTextureData {
  static constexpr unsigned AtlasTileSize = 64;
  uint64_t id;
  Image pixels;
  TextureAddressing addressing;
  TextureFiltering filtering;
  // Atlas versions share immutable tiles instead of copying whole pages.
  Vec2U atlasSize{};
  shared_ptr<uint8_t const> atlasOwner;
  std::vector<shared_ptr<Image const>> atlasTiles;
  // Dynamic effect images may reuse completed GPU allocations. Pixel versions
  // remain immutable so several updates in one frame still render correctly.
  bool lighting = false;
  Vec2U size() const { return atlasOwner ? atlasSize : pixels.size(); }
};
using VulkanTextureDataPtr = shared_ptr<VulkanTextureData const>;

// Snapshots ordered draws; the device integration uploads and records commands.
// Atlas pages use immutable versions. Compression/relocation is disabled.
class VulkanRenderer : public Renderer {
public:
  struct WorldState {
    bool lightMapEnabled = true;
    Vec2F lightMapScale = {1, 1};
    Vec2F lightMapOffset{};
    float lightMapMultiplier = 1;
    std::array<Vec3F, 6> parameters{};
    VulkanTextureDataPtr lightMap, tileLightMap;
    bool operator==(WorldState const& other) const {
      return lightMapEnabled == other.lightMapEnabled && lightMapScale == other.lightMapScale
          && lightMapOffset == other.lightMapOffset && lightMapMultiplier == other.lightMapMultiplier
          && parameters == other.parameters && lightMap == other.lightMap && tileLightMap == other.tileLightMap;
    }
  };
  struct Vertex {
    RenderVertex vertex;
    uint32_t textureIndex;
  };
  struct Draw {
    uint32_t firstVertex;
    uint32_t vertexCount;
    Mat3F transform;
    Maybe<RectI> scissor;
    // The shader supports at most four image slots. Inline storage avoids a
    // heap allocation for every immediate batch and cached-buffer replay.
    StaticVector<VulkanTextureDataPtr, 4> textures;
    WorldState effect;
    bool world = false;
    bool composite = false;
  };

  explicit VulkanRenderer(bool gameplay = false);

  String rendererId() const override;
  bool supportsWorldRendering() const override;
  bool worldEnabled() const;
  Vec2U screenSize() const override;
  void setScreenSize(Vec2U size) override;
  void startFrame() override;
  void finishFrame() override;
  void loadConfig(Json const&) override;
  void loadEffectConfig(String const&, Json const&, StringMap<String> const&) override;
  void setEffectParameter(String const&, RenderEffectParameter const&) override;
  void setEffectTexture(String const&, Image const&) override;
  bool switchEffectConfig(String const&) override;
  void setScissorRect(Maybe<RectI> const&) override;
  TexturePtr createTexture(Image const&, TextureAddressing, TextureFiltering) override;
  void setSizeLimitEnabled(bool) override;
  void setMultiTexturingEnabled(bool) override;
  TextureGroupPtr createTextureGroup(TextureGroupSize, TextureFiltering) override;
  RenderBufferPtr createRenderBuffer() override;
  List<RenderPrimitive>& immediatePrimitives() override;
  void render(RenderPrimitive primitive) override;
  void renderBuffer(RenderBufferPtr const&, Mat3F const&) override;
  void flush() override;

  std::vector<Vertex> const& vertices() const;
  std::vector<Draw> const& draws() const;
  VulkanTextureDataPtr const& whiteTexture() const;

private:
  void mergeAdjacentDraws();
  void append(List<RenderPrimitive> const&, Mat3F const&, std::vector<std::vector<TexturePtr>>* bindings = nullptr);
  Vec2U m_size{};
  Maybe<RectI> m_scissor;
  List<RenderPrimitive> m_immediate;
  std::vector<Vertex> m_vertices;
  std::vector<Draw> m_draws;
  VulkanTextureDataPtr m_white;
  VulkanTextureDataPtr m_transparent;
  bool m_multiTexturing = true;
  bool m_limitTextureGroupSize = false;
  bool m_interfaceLoaded = false;
  bool m_worldLoaded = false;
  bool m_currentWorld = false;
  bool m_worldComposited = false;
  bool m_gameplay = false;
  WorldState m_worldState;
#ifdef TRACY_ENABLE
  std::vector<std::weak_ptr<TextureGroup>> m_liveTextureGroups;
#endif
};

}

#endif
