#version 460 core

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexcoord0;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform MaterialData {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	float AlphaCutoff;
	float MetallicFactor;
	float RoughnessFactor;
} Material;

void main() {
	outColor = vec4(0.8f, 0.8f, 0.8f, 1.0f);
	outColor = vec4(inNormal, 1.0f);
	outColor = vec4(inTexcoord0, 0.0f, 1.0f);
	outColor = vec4(Material.BaseColorFactor);
}
