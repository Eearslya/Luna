#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D HDR;
layout(set = 0, binding = 1) uniform sampler2D Bloom;
layout(set = 0, binding = 2) uniform sampler3D LUT;
layout(set = 0, binding = 3) uniform LuminanceData {
	float AverageLogLuminance;
	float AverageLinearLuminance;
	float AverageInvLinearLuminance;
};

layout(push_constant) uniform PushConstant {
	float Exposure;
	bool DynamicExposure;
};

layout(location = 0) out vec4 outColor;

const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;
const float W = 11.2;

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

vec3 TonyMcMapface(vec3 color) {
	const float lutDims = 48.0;
	vec3 encoded = color / (color + 1.0);
	vec3 uv = encoded * ((lutDims - 1.0) / lutDims) + 0.5 / lutDims;

	return textureLod(LUT, uv, 0).rgb;
}

void main() {
	vec3 color = textureLod(HDR, inUV, 0).rgb;
	vec3 bloom = textureLod(Bloom, inUV, 0).rgb;
	color += bloom;

	float dynamicExposure = DynamicExposure ? AverageInvLinearLuminance : 1.0f;
	outColor = vec4(TonyMcMapface(color * (dynamicExposure * Exposure)), 1.0);
}
