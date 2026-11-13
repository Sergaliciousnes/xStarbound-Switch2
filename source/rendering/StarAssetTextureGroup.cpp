#include "StarAssetTextureGroup.hpp"
#include "StarSwitch2ExplorationProfile.hpp"
#include "StarAssets.hpp"
#include "StarImageMetadataDatabase.hpp"
#include "StarIterator.hpp"
#include "StarRoot.hpp"
#include "StarTime.hpp"
#include "StarRenderProfiling.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Star {

AssetTextureGroup::AssetTextureGroup(TextureGroupPtr textureGroup, uint64_t retainedIdleBytes)
    : m_textureGroup(std::move(textureGroup)), m_retainedIdleBytes(retainedIdleBytes) {
  m_reloadTracker = make_shared<TrackerListener>();
  Root::singleton().registerReloadListener(m_reloadTracker);
}

TexturePtr AssetTextureGroup::loadTexture(AssetPath const& imagePath) {
  return loadTexture(imagePath, false);
}

TexturePtr AssetTextureGroup::tryTexture(AssetPath const& imagePath) {
  return loadTexture(imagePath, true);
}

bool AssetTextureGroup::textureLoaded(AssetPath const& imagePath) const {
  return m_textureMap.contains(imagePath);
}

void AssetTextureGroup::cleanup(int64_t textureTimeout) {
  STAR_RENDER_ZONE("Asset texture cache cleanup");
  if (m_reloadTracker->pullTriggered()) {
    m_textureMap.clear();
    m_textureDeduplicationMap.clear();
    m_usedTextures.clear();

  } else if (m_retainedIdleBytes) {
    STAR_RENDER_ZONE("Parallax bounded idle texture cache");
    // cleanup follows rendering. Keep the working set, then retain the most
    // recently used idle textures that fit the budget. Count aliases once.
    std::unordered_map<Texture const*, int64_t> lastUse;
    for (auto const& entry : m_textureMap) {
      auto& time = lastUse[entry.second.first.get()];
      time = std::max(time, entry.second.second);
    }
    std::vector<std::pair<Texture const*, int64_t>> idle;
    uint64_t idleBytes = 0;
    for (auto const& entry : lastUse) {
      if (!m_usedTextures.count(entry.first)) {
        idle.push_back(entry);
        auto size = entry.first->size();
        idleBytes += (uint64_t)size[0] * size[1] * 4;
      }
    }
    // Most frames are already within budget. No eviction or ordering is
    // needed, so avoid sorting and filtering the cache on those frames.
    if (idleBytes <= m_retainedIdleBytes) {
      STAR_RENDER_PLOT("Parallax idle retained bytes", idleBytes);
      STAR_RENDER_PLOT("Parallax idle evictions/frame", 0);
      m_usedTextures.clear();
      return;
    }
    std::sort(idle.begin(), idle.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
    uint64_t retained = 0;
    size_t evicted = 0;
    for (auto const& entry : idle) {
      auto size = entry.first->size();
      uint64_t bytes = (uint64_t)size[0] * size[1] * 4;
      if (bytes <= m_retainedIdleBytes - retained) {
        retained += bytes;
        m_usedTextures.insert(entry.first);
      } else {
        ++evicted;
      }
    }
    filter(m_textureMap, [&](auto const& entry) { return m_usedTextures.count(entry.second.first.get()) != 0; });
    eraseWhere(m_textureDeduplicationMap, [&](auto const& entry) { return m_usedTextures.count(entry.second.get()) == 0; });
    STAR_RENDER_PLOT("Parallax idle retained bytes", retained);
    STAR_RENDER_PLOT("Parallax idle evictions/frame", evicted);
    m_usedTextures.clear();
  } else {
    int64_t time = Time::monotonicMilliseconds();

    // @LumiKalt: This is called every frame.
    // A pass filters the whole map, sorts the live textures and prunes the deduplication map, which costs far more than a lookup once many textures are loaded, but
    // textures only expire after textureTimeout.
    // Walking them a tenth of the timeout (at most once a second) apart frees them at most that much later and skips the work on the frames in between.
    int64_t interval = clamp<int64_t>(textureTimeout / 10, 0, 1000);
    if (time - m_lastCleanup < interval)
      return;
    m_lastCleanup = time;

    List<Texture const*> liveTextures;
    filter(m_textureMap, [&](auto const& pair) {
      if (time - pair.second.second < textureTimeout) {
        liveTextures.append(pair.second.first.get());
        return true;
      }
      return false;
    });

    liveTextures.sort();

    eraseWhere(m_textureDeduplicationMap, [&](auto const& p) {
      return !liveTextures.containsSorted(p.second.get());
    });
  }
}

TexturePtr AssetTextureGroup::loadTexture(AssetPath const& imagePath, bool tryTexture) {
  if (auto p = m_textureMap.ptr(imagePath)) {
    p->second = Time::monotonicMilliseconds();
    if (m_retainedIdleBytes) m_usedTextures.insert(p->first.get());
    return p->first;
  }

  auto assets = Root::singleton().assets();
  STAR_EXPLORATION_SCOPE(missTrace,Switch2ExplorationProfile::Kind::TextureMiss,imagePath.basePath.utf8Ptr());
  STAR_RENDER_ZONE("Asset texture cache miss");
  STAR_RENDER_COUNT(textureCacheMisses, 1);
  STAR_RENDER_TEXT("%s", imagePath.basePath.utf8().c_str());

  ImageConstPtr image;
  try { // FezzedOne: Fixed a crash that could occur when an invalid asset path is used for texture rendering.
    STAR_RENDER_ZONE("Asset texture image request");
    if (tryTexture)
      image = assets->tryImage(imagePath);
    else
      image = assets->image(imagePath);
  } catch (AssetException const& e) {
    // Logger::warn("AssetTextureGroup: Unable to load image '{}' due to exception: {}", AssetPath::join(imagePath), e.what());
    return {};
  }

  if (!image)
    return {};

  // Assets will return the same image ptr if two different asset paths point
  // to the same underlying cached image.  We should not make duplicate entries
  // in the texture group for these, so we keep track of the image pointers
  // returned to deduplicate them.
  if (auto existingTexture = m_textureDeduplicationMap.value(image)) {
    if (m_retainedIdleBytes) m_usedTextures.insert(existingTexture.get());
    m_textureMap.add(imagePath, {existingTexture, Time::monotonicMilliseconds()});
    return existingTexture;
  } else {
    STAR_RENDER_ZONE("Asset texture renderer creation");
    auto texture = m_textureGroup->create(*image);
    if (m_retainedIdleBytes) m_usedTextures.insert(texture.get());
    m_textureMap.add(imagePath, {texture, Time::monotonicMilliseconds()});
    m_textureDeduplicationMap.add(image, texture);
    return texture;
  }
}

} // namespace Star
