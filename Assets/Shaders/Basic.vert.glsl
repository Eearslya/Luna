#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;

layout(set = 0, binding = 0) uniform CameraData {
	mat4 Projection;
	mat4 View;
	vec3 Position;
} Camera;

layout(push_constant) uniform ModelData {
	mat4 Transform;
} Model;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV0;

void main() {
	vec4 locPos;
	locPos = Model.Transform * mat4(1.0) * vec4(inPosition, 1.0);
	outNormal = normalize(transpose(inverse(mat3(Model.Transform * mat4(1.0)))) * inNormal);
	outWorldPos = locPos.xyz / locPos.w;
	outUV0 = inUV0;

	gl_Position = Camera.Projection * Camera.View * vec4(outWorldPos, 1.0);
}
