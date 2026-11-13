#ifndef STAR_SDL_RENDER_BACKEND_HPP
#define STAR_SDL_RENDER_BACKEND_HPP

#include "StarRenderer.hpp"
#include "SDL.h"

namespace Star {

// Desktop window-system integration. Renderer itself remains independent of
// SDL, so another platform can provide its own graphics integration.
class SdlRenderBackend {
public:
  virtual ~SdlRenderBackend() = default;

  virtual Uint32 windowFlags() const = 0;
  virtual void initialize(SDL_Window* window) = 0;
  virtual RendererPtr createRenderer() = 0;
  virtual void present() = 0;
  virtual void setVSyncEnabled(bool enabled) = 0;
};

// Selects the requested backend. Destroy all renderer resources before destroying
// this integration, and destroy the integration before its SDL window.
unique_ptr<SdlRenderBackend> createSdlRenderBackend(bool vulkanMenus = false, bool vulkanGameplay = false);

}

#endif
