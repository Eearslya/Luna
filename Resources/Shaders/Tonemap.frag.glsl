#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D HDR;
layout(set = 0, binding = 1) uniform sampler2D Bloom;

layout(push_constant) uniform PushConstant {
	float Exposure;
};

layout(location = 0) out vec4 outColor;

const mediump float A = 0.15;
const mediump float B = 0.50;
const mediump float C = 0.10;
const mediump float D = 0.20;
const mediump float E = 0.02;
const mediump float F = 0.30;
const mediump float W = 11.2;

vec3 TonemapUncharted2(vec3 x) {
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float TonemapUncharted2(float x) {
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 TonemapFilmic(vec3 color) {
	vec3 curr = TonemapUncharted2(color);
	float whiteScale = 1.0 / TonemapUncharted2(W);

	return curr * whiteScale;
}

void main() {
	vec3 color = textureLod(HDR, inUV, 0).rgb;
	vec3 bloom = textureLod(Bloom, inUV, 0).rgb;
	color += bloom;

	outColor = vec4(TonemapFilmic(color) * Exposure, 1.0);
}
