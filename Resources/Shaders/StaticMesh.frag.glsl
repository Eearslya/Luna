#version 460 core

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "Srgb.glsli"

layout(location = 0) in vec2 inTexcoord0;
layout(location = 1) in mat3 inTBN;

layout(set = 1, binding = 0) uniform sampler2D Textures[];

layout(set = 2, binding = 0) uniform Material {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	float RoughnessFactor;
	float MetallicFactor;
	uint Albedo;
	uint Normal;
	uint PBR;
	uint Occlusion;
	uint Emissive;
};

layout(location = 0) out vec4 outColor;

void main() {
	vec4 baseColor = texture(Textures[nonuniformEXT(Albedo)], inTexcoord0);
	if (baseColor.a < 0.5f) { discard; }
	baseColor *= BaseColorFactor;

	vec3 N = normalize(texture(Textures[nonuniformEXT(Normal)], inTexcoord0).rgb * 2.0f - 1.0f);
	N = normalize(inTBN * N);

	outColor = SrgbToLinear(vec4(N * 0.5 + 0.5, 1));
}
