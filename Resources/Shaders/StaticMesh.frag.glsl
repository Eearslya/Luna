#version 460 core

#include "Srgb.glsli"

layout(location = 0) in vec2 inTexcoord0;
layout(location = 1) in mat3 inTBN;

layout(set = 1, binding = 1) uniform MaterialData {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	float AlphaCutoff;
	float MetallicFactor;
	float RoughnessFactor;
} Material;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 L = normalize(vec3(5, 2, 3));
	vec3 N = normalize(inTBN[2]);
	float NdotL = max(dot(N, L), 0.0);
	float lambert = NdotL * 0.5 + 0.5;
	float halfLambert = lambert * lambert;

	outColor = SrgbToLinear(vec4(N * 0.5 + 0.5, 1));
	outColor = vec4(vec3(halfLambert), 1);
	outColor = vec4(Material.BaseColorFactor.rgb * halfLambert, Material.BaseColorFactor.a);
}
