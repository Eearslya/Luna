#version 450 core

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inTexCoords;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = texture(uTexture, inTexCoords);
}
