#version 460 core

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "Normal.glsli"
#include "Srgb.glsli"

layout(location = 0) flat in uint inMaterialIndex;
layout(location = 1) in vec2 inUV0;
layout(location = 2) in mat3 inTBN;

layout(set = 1, binding = 0) uniform sampler2D Textures[];

struct Material {
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

layout(set = 2, binding = 0, scalar) buffer readonly MaterialData {
	Material M[];
} Materials;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outPBR;
layout(location = 3) out vec4 outEmissive;

void main() {
	Material mat = Materials.M[inMaterialIndex];

	vec4 baseColor = texture(Textures[nonuniformEXT(mat.Albedo)], inUV0);
	if (baseColor.a < 0.5f) { discard; }
	outAlbedo = vec4(baseColor.rgb * mat.BaseColorFactor.rgb, baseColor.a);

	vec3 N = normalize(texture(Textures[nonuniformEXT(mat.Normal)], inUV0).rgb * 2.0f - 1.0f);
	N = normalize(inTBN * N);
	outNormal = vec4(EncodeNormal(N), 0, 0);

	vec3 orm = texture(Textures[nonuniformEXT(mat.PBR)], inUV0).xyz;
	outPBR = vec4(0, orm.g * mat.RoughnessFactor, orm.b * mat.MetallicFactor, 0);

	vec3 emissive = texture(Textures[nonuniformEXT(mat.Emissive)], inUV0).rgb;
	outEmissive = vec4(emissive * mat.EmissiveFactor.rgb, 0);
}
