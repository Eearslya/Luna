#version 460 core
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"

struct DebugLine {
  vec3 Start;
  vec3 End;
  vec3 Color;
};

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0, scalar) restrict readonly buffer DebugLinesBuffer {
  DebugLine Lines[];
};

void main() {
  const DebugLine line = Lines[gl_VertexIndex / 2];
  const vec3 position = ((gl_VertexIndex & 1) == 1) ? line.End : line.Start;
  gl_Position = /*Scene.ViewProjection **/ vec4(position.xy * 2.0 - 1.0, 0.0, 1.0);
}
