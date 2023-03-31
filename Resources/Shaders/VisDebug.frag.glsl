#version 460 core

#include "Color.glsl"

layout(set = 0, binding = 0, input_attachment_index = 0) uniform usubpassInput Visibility;

layout(location = 0) out vec4 outColor;

// 0: Triangle ID
// 1: Object ID
// 2: Object Masking
const uint Debug = 0;

void main() {
	uint vis = subpassLoad(Visibility).r;
	uint masked = (vis >> 31);
	uint objectId = (vis >> 23) & 0xff;
	uint triangleId = vis & 0x7fffff;

	if (Debug == 0) {
		outColor = DebugColorUint(triangleId);
	} else if (Debug == 1) {
		outColor = DebugColorUint(objectId);
	} else if (Debug == 2) {
		vec3 color = masked == 1 ? vec3(0.2, 1.0, 0.2) : vec3(1.0, 0.2, 0.2);
		outColor = vec4(color, 1.0);
	} else {
		outColor = vec4(0, 0, 0, 1);
	}
}
