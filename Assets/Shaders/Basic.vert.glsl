#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoords;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outTexCoords;

void main() {
	outColor = inColor;
	outTexCoords = inTexCoords;
	gl_Position = vec4(inPosition, 1.0f);
}
