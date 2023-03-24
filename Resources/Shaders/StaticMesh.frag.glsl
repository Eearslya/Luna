#version 460 core

#include "Srgb.glsli"

layout(location = 0) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = SrgbToLinear(vec4(inNormal * 0.5f + 0.5f, 1.0f));
}
