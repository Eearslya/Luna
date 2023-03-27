#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "Normal.glsli"
#include "Srgb.glsli"

layout(location = 0) in vec2 inUV0;
layout(location = 1) in mat3 inTBN;

layout(set = 1, binding = 0) uniform sampler2D Textures[];

layout(set = 2, binding = 0) uniform MaterialData {
	uint Albedo;
	uint Normal;
} Material;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
	vec4 baseColor = texture(Textures[nonuniformEXT(Material.Albedo)], inUV0);
	if (baseColor.a < 0.5f) { discard; }
	outAlbedo = baseColor;

	vec3 N = normalize(texture(Textures[nonuniformEXT(Material.Normal)], inUV0).rgb * 2.0f - 1.0f);
	N = normalize(inTBN * N);
	outNormal = vec4(EncodeNormal(N), 0, 0);
}
