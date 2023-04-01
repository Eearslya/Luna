#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D HDR;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 color = textureLod(HDR, inUV, 0).rgb;
	float luminance = max(max(color.r, color.g), color.b) + 0.0001;

	float logLum = log2(luminance);
	color /= luminance;

	luminance -= 8.0;

	vec3 threshold = max(color * luminance, vec3(0));

	outColor = vec4(threshold, 1);
}
