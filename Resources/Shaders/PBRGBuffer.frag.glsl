#version 460 core

#include "Normal.glsli"
#include "Srgb.glsli"

layout(location = 0) in vec2 inUV0;
layout(location = 1) in mat3 inTBN;

layout(set = 1, binding = 0) uniform sampler2D TexAlbedo;
layout(set = 1, binding = 1) uniform sampler2D TexNormal;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
	vec4 baseColor = texture(TexAlbedo, inUV0);
	if (baseColor.a < 0.5f) { discard; }
	outAlbedo = baseColor;

	vec3 N = normalize(texture(TexNormal, inUV0).rgb * 2.0f - 1.0f);
	N = normalize(inTBN * N);
	outNormal = vec4(EncodeNormal(N), 0, 0);
}
