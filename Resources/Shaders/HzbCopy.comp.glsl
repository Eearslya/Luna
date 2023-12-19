#version 460 core

#include "Common.glsli"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D In;
layout(set = 0, binding = 1, r32f) uniform restrict writeonly image2D Out;

void main() {
  const vec2 position = vec2(gl_GlobalInvocationID.xy);
  const vec2 hzbSize = vec2(imageSize(Out));
  const vec2 uv = (position + 0.5) / hzbSize;
  const float depthSample = texture(In, uv).r;
  imageStore(Out, ivec2(position), vec4(depthSample));
}
