#include "StarRendererFactory.hpp"
#include "StarRenderer_opengl20.hpp"

namespace Star {

RendererPtr createOpenGl20Renderer() {
  return make_shared<OpenGl20Renderer>();
}

}
