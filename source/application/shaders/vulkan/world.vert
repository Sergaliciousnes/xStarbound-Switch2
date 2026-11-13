#version 450
layout(location = 0) in vec2 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 vertexTextureCoordinate;
layout(location = 3) in uint vertexTextureIndex;
layout(location = 4) in float vertexParam1;
layout(location = 0) out vec4 fragmentColor;
layout(location = 1) out vec2 fragmentTextureCoordinate;
layout(location = 2) flat out uint fragmentTextureIndex;
layout(location = 3) out vec2 fragmentLightMapCoordinate;
layout(location = 4) out float fragmentLightMapMultiplier;
layout(push_constant) uniform DrawState {
  vec4 row0; vec4 row1; vec4 row2; vec4 screenSize;
} state;
layout(set = 0, binding = 6, std140) uniform WorldState {
  vec4 mapTransform;
  vec4 lighting;
  vec4 parameters[6];
  vec4 screenSize;
} effect;
void main() {
  vec3 p = vec3(vertexPosition, 1.0);
  vec2 screenPosition = vec2(dot(state.row0.xyz, p), dot(state.row1.xyz, p));
  fragmentLightMapCoordinate = screenPosition / effect.mapTransform.xy
      - effect.mapTransform.zw * effect.lighting.zw / effect.screenSize.xy;
  gl_Position = vec4(screenPosition.x / state.screenSize.x * 2.0 - 1.0,
                    1.0 - screenPosition.y / state.screenSize.y * 2.0, 0.0, 1.0);
  fragmentTextureCoordinate = vertexTextureCoordinate;
  fragmentTextureIndex = vertexTextureIndex;
  fragmentColor = vertexColor;
  fragmentLightMapMultiplier = vertexParam1;
}
