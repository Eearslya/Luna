#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D Sample;

layout(push_constant, std430) uniform PushConstant {
	vec2 InvTexelSize;
};

layout(location = 0) out vec4 outColor;

void main() {
	vec4 value = 0.25 * textureLod(Sample, inUV, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(-0.875, +0.875) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(+0.000, +0.875) * InvTexelSize, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(+0.875, +0.875) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(-0.875, +0.000) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(+0.875, +0.000) * InvTexelSize, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(-0.875, -0.875) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(+0.000, -0.875) * InvTexelSize, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(+0.875, -0.875) * InvTexelSize, 0);

	outColor = value;
}
