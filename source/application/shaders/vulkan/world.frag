#version 450
layout(location = 0) in vec4 fragmentColor;
layout(location = 1) in vec2 fragmentTextureCoordinate;
layout(location = 2) flat in uint fragmentTextureIndex;
layout(location = 3) in vec2 fragmentLightMapCoordinate;
layout(location = 4) in float fragmentLightMapMultiplier;
layout(location = 0) out vec4 outputColor;
layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(set = 0, binding = 1) uniform sampler2D texture1;
layout(set = 0, binding = 2) uniform sampler2D texture2;
layout(set = 0, binding = 3) uniform sampler2D texture3;
layout(set = 0, binding = 4) uniform sampler2D lightMap;
layout(set = 0, binding = 5) uniform sampler2D tileLightMap;
layout(set = 0, binding = 6, std140) uniform WorldState {
  vec4 mapTransform; // scale.xy, offset.xy
  vec4 lighting;     // enabled, multiplier, lightmap width/height
  vec4 parameters[6];
  vec4 screenSize;
} effect;

vec4 cubic(float v) {
  vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
  vec4 s = n * n * n;
  return vec4(s.x, s.y - 4.0 * s.x, s.z - 4.0 * s.y + 6.0 * s.x,
              6.0 - (s.x + s.y - 4.0 * s.x + s.z - 4.0 * s.y + 6.0 * s.x));
}
vec4 bicubicSample(sampler2D source, vec2 texcoord, vec2 texscale) {
  texcoord -= vec2(0.5);
  vec2 f = fract(texcoord);
  texcoord -= f;
  vec4 xcubic = cubic(f.x), ycubic = cubic(f.y);
  vec4 c = vec4(texcoord.x - 0.5, texcoord.x + 1.5, texcoord.y - 0.5, texcoord.y + 1.5);
  vec4 s = vec4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
  vec4 offset = c + vec4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;
  vec4 a = texture(source, vec2(offset.x, offset.z) * texscale);
  vec4 b = texture(source, vec2(offset.y, offset.z) * texscale);
  vec4 c0 = texture(source, vec2(offset.x, offset.w) * texscale);
  vec4 d = texture(source, vec2(offset.y, offset.w) * texscale);
  return mix(mix(d, c0, s.x / (s.x + s.y)), mix(b, a, s.x / (s.x + s.y)), s.z / (s.z + s.w));
}
void main() {
  vec2 dx = dFdx(fragmentTextureCoordinate), dy = dFdy(fragmentTextureCoordinate);
  vec4 texel;
  if (fragmentTextureIndex == 0u) texel = textureGrad(texture0, fragmentTextureCoordinate, dx, dy);
  else if (fragmentTextureIndex == 1u) texel = textureGrad(texture1, fragmentTextureCoordinate, dx, dy);
  else if (fragmentTextureIndex == 2u) texel = textureGrad(texture2, fragmentTextureCoordinate, dx, dy);
  else texel = textureGrad(texture3, fragmentTextureCoordinate, dx, dy);
  if (texel.a <= 0.0) discard;
  vec4 color = texel * fragmentColor;
  bool enabled = effect.lighting.x != 0.0;
  float multiplier = fragmentLightMapMultiplier * effect.lighting.y;
  if (texel.a == 254.0 / 255.0) color.a = fragmentColor.a;
  else if (enabled && multiplier > 0.0) {
    vec4 a = bicubicSample(lightMap, fragmentLightMapCoordinate, 1.0 / effect.lighting.zw);
    vec4 b = bicubicSample(tileLightMap, fragmentLightMapCoordinate, 1.0 / effect.lighting.zw);
    vec3 light = b.a <= 0.0 ? a.rgb : mix(a.rgb, b.rgb / b.a, b.a);
    color.rgb *= light * multiplier;
  }
  if (enabled) {
    color.rgb *= effect.parameters[0].x + 1.0;
    color.rgb = mix(color.rgb, vec3(0.5), effect.parameters[0].y);
    color.rgb = mix(color.rgb, vec3(dot(color.rgb, vec3(0.2126, 0.7152, 0.0722))), effect.parameters[0].z);
    if (effect.parameters[2].x != 0.0) color.rgb *= effect.parameters[1].xyz;
  }
  outputColor = color;
}
