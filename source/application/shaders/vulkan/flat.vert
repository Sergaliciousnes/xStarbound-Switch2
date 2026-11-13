#version 450

layout(location = 0) in vec2 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 vertexTextureCoordinate;
layout(location = 3) in uint vertexTextureIndex;
layout(location = 0) out vec4 fragmentColor;
layout(location = 1) out vec2 fragmentTextureCoordinate;
layout(location = 2) flat out uint fragmentTextureIndex;

// Explicit rows avoid mat3 padding and row/column-major ambiguity. This block
// is 64 bytes in both C++ and SPIR-V and fits the Vulkan minimum push limit.
layout(push_constant) uniform DrawState {
  vec4 transformRow0;
  vec4 transformRow1;
  vec4 transformRow2;
  vec4 screenSize;
} state;

void main() {
  vec3 position = vec3(vertexPosition, 1.0);
  vec2 screenPosition = vec2(dot(state.transformRow0.xyz, position),
                            dot(state.transformRow1.xyz, position));
  // The engine is bottom-left origin; the Vulkan viewport is top-left origin.
  gl_Position = vec4(screenPosition.x / state.screenSize.x * 2.0 - 1.0,
                    1.0 - screenPosition.y / state.screenSize.y * 2.0, 0.0, 1.0);
  fragmentColor = vertexColor;
  fragmentTextureCoordinate = vertexTextureCoordinate;
  fragmentTextureIndex = vertexTextureIndex;
}
