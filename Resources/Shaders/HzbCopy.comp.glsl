#version 460 core

#include "Common.glsli"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D Depth;
layout(set = 0, binding = 1, r32f) uniform restrict writeonly image2D HZB;

void main() {
  const vec2 position = vec2(gl_GlobalInvocationID.xy);
  const vec2 hzbSize = vec2(imageSize(HZB));
  const vec2 uv = (position + 0.5) / hzbSize;
  const float[] depth = float[](
    textureLodOffset(Depth, uv, 0.0, ivec2(0.0, 0.0)).r,
    textureLodOffset(Depth, uv, 0.0, ivec2(1.0, 0.0)).r,
    textureLodOffset(Depth, uv, 0.0, ivec2(0.0, 1.0)).r,
    textureLodOffset(Depth, uv, 0.0, ivec2(1.0, 1.0)).r
  );
  const float depthSample = min(min(min(depth[0], depth[1]), depth[2]), depth[3]);
  imageStore(HZB, ivec2(position), vec4(depthSample));
}
