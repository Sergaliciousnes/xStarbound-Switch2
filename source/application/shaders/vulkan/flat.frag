#version 450
layout(location = 0) in vec4 fragmentColor;
layout(location = 1) in vec2 fragmentTextureCoordinate;
layout(location = 2) flat in uint fragmentTextureIndex;
// Fixed bindings avoid optional descriptor indexing. Compute derivatives before
// branching, so different texture choices within a fragment quad are valid.
layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(set = 0, binding = 1) uniform sampler2D texture1;
layout(set = 0, binding = 2) uniform sampler2D texture2;
layout(set = 0, binding = 3) uniform sampler2D texture3;
layout(location = 0) out vec4 outputColor;
void main() {
  vec2 dx = dFdx(fragmentTextureCoordinate), dy = dFdy(fragmentTextureCoordinate);
  vec4 texel;
  if (fragmentTextureIndex == 0u) texel = textureGrad(texture0, fragmentTextureCoordinate, dx, dy);
  else if (fragmentTextureIndex == 1u) texel = textureGrad(texture1, fragmentTextureCoordinate, dx, dy);
  else if (fragmentTextureIndex == 2u) texel = textureGrad(texture2, fragmentTextureCoordinate, dx, dy);
  else texel = textureGrad(texture3, fragmentTextureCoordinate, dx, dy);
  if (texel.a <= 0.0) discard;
  outputColor = texel * fragmentColor;
}
