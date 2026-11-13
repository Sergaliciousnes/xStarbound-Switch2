#ifndef STAR_RENDERER_FACTORY_HPP
#define STAR_RENDERER_FACTORY_HPP

#include "StarRenderer.hpp"

namespace Star {

// Requires a current OpenGL context. The platform must keep that context alive
// until the renderer and all of its resources have been destroyed.
RendererPtr createOpenGl20Renderer();

}

#endif
