#version 460 core

#include "Srgb.glsli"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV0;

layout(set = 1, binding = 0) uniform sampler2D TexAlbedo;

layout(location = 0) out vec4 outColor;

void main() {
	vec4 baseColor = texture(TexAlbedo, inUV0);
	if (baseColor.a < 0.5f) { discard; }

	outColor = baseColor;
}
