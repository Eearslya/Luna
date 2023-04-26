#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord0;

layout(set = 0, binding = 0) uniform CameraData {
	mat4 ViewProjection;
	mat4 InvViewProjection;
	mat4 Projection;
	mat4 InvProjection;
	mat4 View;
	mat4 InvView;
	vec4 Position;
} Camera;

layout(push_constant) uniform PushConstant {
	mat4 Model;
};

const vec3 Positions[3] = vec3[3](
	vec3(-1.0f, -1.0f, 0.0f),
	vec3(1.0f, -1.0f, 0.0f),
	vec3(0.0f, 1.0f, 0.0f)
);

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexcoord0;

void main() {
	gl_Position = Camera.ViewProjection * Model * vec4(inPosition, 1.0f);
	outNormal = inNormal;
	outTexcoord0 = inTexcoord0;
}
