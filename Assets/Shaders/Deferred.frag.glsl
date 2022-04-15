#version 450 core

layout(location = 0) in vec2 inUV0;

layout(set = 0, binding = 0) uniform CameraData {
	mat4 Projection;
	mat4 View;
	mat4 ViewInverse;
	vec3 Position;
} Camera;

layout(set = 0, binding = 1) uniform SceneData {
	vec4 SunDirection;
	float PrefilteredCubeMipLevels;
	float Exposure;
	float Gamma;
	float IBLContribution;
} Scene;

layout(set = 0, binding = 2) uniform samplerCube TexIrradiance;
layout(set = 0, binding = 3) uniform samplerCube TexPrefiltered;
layout(set = 0, binding = 4) uniform sampler2D TexBrdfLut;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput InputPosition;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput InputNormal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput InputAlbedo;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput InputPBR;
layout(input_attachment_index = 4, set = 1, binding = 4) uniform subpassInput InputEmissive;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = vec4(subpassLoad(InputAlbedo).rgb, 1);
}
