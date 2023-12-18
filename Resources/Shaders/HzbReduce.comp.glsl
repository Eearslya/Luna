#version 460 core

#include "Common.glsli"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, r32f) uniform restrict readonly image2D HZBSrc;
layout(set = 0, binding = 1, r32f) uniform restrict writeonly image2D HZBDst;

void main() {
  const vec2 position = vec2(gl_GlobalInvocationID.xy);
  const vec2 hzbSrcSize = vec2(imageSize(HZBSrc));
  const vec2 hzbDstSize = vec2(imageSize(HZBDst));
  const vec2 scaledPos = position * (hzbSrcSize / hzbDstSize);
  const float[] depths = float[](
    imageLoad(HZBSrc, ivec2(scaledPos + vec2(0.0, 0.0) + 0.5)).r,
    imageLoad(HZBSrc, ivec2(scaledPos + vec2(1.0, 0.0) + 0.5)).r,
    imageLoad(HZBSrc, ivec2(scaledPos + vec2(0.0, 1.0) + 0.5)).r,
    imageLoad(HZBSrc, ivec2(scaledPos + vec2(1.0, 1.0) + 0.5)).r
  );
  const float depth = min(min(min(depths[0], depths[1]), depths[2]), depths[3]);
  imageStore(HZBDst, ivec2(position), vec4(depth));
}