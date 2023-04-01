#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D Sample;

layout(location = 0) out vec4 outColor;

layout(push_constant, std430) uniform PushConstant {
	vec2 InvTexelSize;
};

void main() {
	vec4 value = 0.25 * textureLod(Sample, inUV, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(-1.75, +1.75) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(+0.00, +1.75) * InvTexelSize, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(+1.75, +1.75) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(-1.75, +0.00) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(+1.75, +0.00) * InvTexelSize, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(-1.75, -1.75) * InvTexelSize, 0);
	value += 0.1250 * textureLod(Sample, inUV + vec2(+0.00, -1.75) * InvTexelSize, 0);
	value += 0.0625 * textureLod(Sample, inUV + vec2(+1.75, -1.75) * InvTexelSize, 0);

	outColor = value;
}
