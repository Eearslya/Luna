#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 Projection;
	mat4 View;
} Scene;

layout(push_constant) uniform PushConstant {
	mat4 Model;
} PC;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outViewPos;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec2 outUV0;

void main() {
	vec4 locPos;
	locPos = PC.Model * vec4(inPosition, 1.0);
	outNormal = normalize(transpose(inverse(mat3(PC.Model))) * inNormal);
	outWorldPos = locPos.xyz / locPos.w;
	outViewPos = (Scene.View * locPos).xyz;
	outUV0 = inUV0;

	gl_Position = Scene.Projection * Scene.View * vec4(outWorldPos, 1.0);
}
