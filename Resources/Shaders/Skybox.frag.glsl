#version 460 core

layout(location = 0) in vec3 inLocalPos;

layout(set = 1, binding = 0) uniform samplerCube TexSkybox;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 envColor = texture(TexSkybox, inLocalPos).rgb;
	envColor = envColor / (envColor + vec3(1.0));
	outColor = vec4(envColor, 1.0);
}
