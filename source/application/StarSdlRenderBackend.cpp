#include "StarSdlRenderBackend.hpp"
#ifdef STAR_ENABLE_OPENGL
#include "StarRendererFactory.hpp"
#endif
#include "StarLogging.hpp"
#ifdef STAR_ENABLE_VULKAN
#include "StarVulkanDiagnostic.hpp"
#endif

namespace Star {
namespace {

#ifdef STAR_ENABLE_OPENGL
class SdlOpenGlRenderBackend : public SdlRenderBackend {
public:
  ~SdlOpenGlRenderBackend() override {
    if (m_context)
      SDL_GL_DeleteContext(m_context);
  }

  Uint32 windowFlags() const override {
    return SDL_WINDOW_OPENGL;
  }

  void initialize(SDL_Window* window) override {
    if (m_context)
      throw RendererException("OpenGL context already initialized");
    m_context = SDL_GL_CreateContext(window);
    if (!m_context)
      throw RendererException::format("Could not create OpenGL context: {}", SDL_GetError());
    m_window = window;
  }

  RendererPtr createRenderer() override {
    if (!m_context)
      throw RendererException("OpenGL context is not initialized");
    return createOpenGl20Renderer();
  }

  void present() override {
    SDL_GL_SwapWindow(m_window);
  }

  void setVSyncEnabled(bool enabled) override {
    if (enabled) {
      Logger::info("Application: Enabling VSync with late swap tearing");
      if (SDL_GL_SetSwapInterval(-1) < 0) {
        Logger::info("Application: Enabling VSync late swap tearing failed, falling back to full VSync");
        SDL_GL_SetSwapInterval(1);
      }
    } else {
      Logger::info("Application: Disabling VSync");
      SDL_GL_SetSwapInterval(0);
    }
  }

private:
  SDL_Window* m_window = nullptr;
  SDL_GLContext m_context = nullptr;
};
#endif

}

unique_ptr<SdlRenderBackend> createSdlRenderBackend(bool vulkanMenus, bool vulkanGameplay) {
  if (vulkanMenus) {
#ifdef STAR_ENABLE_VULKAN
    return createSdlVulkanMenuBackend(vulkanGameplay);
#else
    throw RendererException("Vulkan support was not built; configure with -DSTAR_ENABLE_VULKAN=ON");
#endif
  }
#ifdef STAR_ENABLE_OPENGL
  return make_unique<SdlOpenGlRenderBackend>();
#else
  throw RendererException("OpenGL support was not built; use Vulkan or configure with -DSTAR_ENABLE_OPENGL=ON");
#endif
}

}
