#version 460 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) buffer LuminanceData {
	float AverageLogLuminance;
	float AverageLinearLuminance;
	float AverageInvLinearLuminance;
};
layout(set = 0, binding = 1) uniform sampler2D Image;

layout(push_constant) uniform PushConstant {
	ivec2 Size;
	float Lerp;
	float MinLogLum;
	float MaxLogLum;
} PC;

shared float SharedLogLum[64];

void main() {
	int iterX = (PC.Size.x + 7) >> 3;
	int iterY = (PC.Size.y + 7) >> 3;
	ivec2 localIndex = ivec2(gl_LocalInvocationID.xy);
	float totalLogLum = 0.0;
	vec2 invSize = 1.0 / vec2(PC.Size);

	for (int y = 0; y < iterY; ++y) {
		for (int x = 0; x < iterX; ++x) {
			int sx = x * 8 + localIndex.x;
			int sy = y * 8 + localIndex.y;
			if (all(lessThan(ivec2(sx, sy), PC.Size))) {
				totalLogLum += textureLod(Image, (vec2(sx, sy) + 0.5) * invSize, 0).a;
			}
		}
	}
	SharedLogLum[gl_LocalInvocationIndex] = totalLogLum;

#define Step(i) \
	memoryBarrierShared(); \
	barrier(); \
	if (gl_LocalInvocationIndex < i) { SharedLogLum[gl_LocalInvocationIndex] += SharedLogLum[gl_LocalInvocationIndex + i]; }
	Step(32u);
	Step(16u);
	Step(8u);
	Step(4u);
	Step(2u);

	memoryBarrierShared();
	barrier();

	if (gl_LocalInvocationIndex == 0u) {
		float loglum = SharedLogLum[0] + SharedLogLum[1];
		loglum *= invSize.x * invSize.y;
		loglum = clamp(loglum, PC.MinLogLum, PC.MaxLogLum);
		float newLogLuma = mix(AverageLogLuminance, loglum, PC.Lerp);
		AverageLogLuminance = newLogLuma;
		AverageLinearLuminance = exp2(newLogLuma);
		AverageInvLinearLuminance = exp2(-newLogLuma);
	}
}
