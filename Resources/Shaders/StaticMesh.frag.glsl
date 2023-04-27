#version 460 core

#include "Srgb.glsli"

layout(location = 0) in vec2 inTexcoord0;
layout(location = 1) in mat3 inTBN;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 N = normalize(inTBN[2]);

	outColor = SrgbToLinear(vec4(N * 0.5 + 0.5, 1));
}
