#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inTexcoord0;

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

void main() {
	vec4 baseColor = texture(Textures[nonuniformEXT(Albedo)], inTexcoord0);
	if (baseColor.a < 0.5f) { discard; }
}
