#version 450 core

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inViewPos;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV0;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = vec4(inNormal + 1.0f * 0.5f, 1.0f);
}
