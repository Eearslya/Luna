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
layout(set = 1, binding = 2) uniform sampler2D TexAlbedo;
layout(set = 1, binding = 3) uniform sampler2D TexNormal;
layout(set = 1, binding = 4) uniform sampler2D TexPBR;
layout(set = 1, binding = 5) uniform sampler2D TexEmissive;

layout(location = 0) out vec4 outColor;

void main() {
	vec4 baseColor = texture(TexAlbedo, inTexcoord0) * Material.BaseColorFactor;
	vec3 L = normalize(vec3(5, 2, 3));
	vec3 N = normalize(texture(TexNormal, inTexcoord0).rgb * 2.0f - 1.0f);
	N = normalize(inTBN * N);
	float NdotL = max(dot(N, L), 0.0);
	float lambert = NdotL * 0.5 + 0.5;
	float halfLambert = lambert * lambert;

	vec3 color = baseColor.rgb * halfLambert;
	color += texture(TexEmissive, inTexcoord0).rgb * Material.EmissiveFactor.rgb;

	outColor = vec4(color, baseColor.a);
}
