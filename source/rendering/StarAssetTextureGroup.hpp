#ifndef STAR_ASSET_TEXTURE_GROUP_HPP
#define STAR_ASSET_TEXTURE_GROUP_HPP

#include "StarAssetPath.hpp"
#include "StarBiMap.hpp"
#include "StarListener.hpp"
#include "StarMaybe.hpp"
#include "StarRenderer.hpp"
#include "StarString.hpp"
#include <unordered_set>

namespace Star {

STAR_CLASS(AssetTextureGroup);

// Creates a renderer texture group for textures loaded directly from Assets.
class AssetTextureGroup {
public:
  // Creates a texture group using the given renderer and textureFiltering for
  // the managed textures.
  // An optional idle retention budget replaces timeout expiry. Intended for
  // standalone textures: logical RGBA size does not describe atlas residency.
  AssetTextureGroup(TextureGroupPtr textureGroup, uint64_t retainedIdleBytes = 0);

  // Load the given texture into the texture group if it is not loaded, and
  // return the texture pointer.
  TexturePtr loadTexture(AssetPath const& imagePath);

  // If the texture is loaded and ready, returns the texture pointer, otherwise
  // queues the texture using Assets::tryImage and returns nullptr.
  TexturePtr tryTexture(AssetPath const& imagePath);

  // Has the texture been loaded?
  bool textureLoaded(AssetPath const& imagePath) const;

  // Frees textures that haven't been used in more than 'textureTimeout' time.
  // With an idle retention budget, keeps this frame's working set and fills
  // the idle budget with the most recently used textures instead.
  // If Root has been reloaded, will simply clear the texture group.
  void cleanup(int64_t textureTimeout);

private:
  // Returns the texture parameters.  If tryTexture is true, then returns none
  // if the texture is not loaded, and queues it, otherwise loads texture
  // immediately
  TexturePtr loadTexture(AssetPath const& imagePath, bool tryTexture);

  TextureGroupPtr m_textureGroup;
  HashMap<AssetPath, pair<TexturePtr, int64_t>> m_textureMap;
  HashMap<ImageConstPtr, TexturePtr> m_textureDeduplicationMap;
  TrackerListenerPtr m_reloadTracker;

  // @LumiKalt: When the last full pass over the textures ran. Expiry is measured in seconds, so
  // cleanup() does not need to walk every texture on every call.
  int64_t m_lastCleanup = 0;
  uint64_t m_retainedIdleBytes = 0;
  std::unordered_set<Texture const*> m_usedTextures;
};

} // namespace Star

#endif
