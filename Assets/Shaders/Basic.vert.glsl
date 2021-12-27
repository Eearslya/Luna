#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 Projection;
	mat4 View;
} Scene;

layout(push_constant) uniform ModelData {
	mat4 Transform;
} Model;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outTexCoord;

void main() {
	outColor = inNormal;
	outTexCoord = inTexCoord;
	const vec4 worldPos = Model.Transform * vec4(inPosition, 1.0f);
	gl_Position = Scene.Projection * Scene.View * worldPos;
}
