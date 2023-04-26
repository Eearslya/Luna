#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord0;

layout(push_constant) uniform PushConstant {
	mat4 MVP;
};

const vec3 Positions[3] = vec3[3](
	vec3(-1.0f, -1.0f, 0.0f),
	vec3(1.0f, -1.0f, 0.0f),
	vec3(0.0f, 1.0f, 0.0f)
);

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexcoord0;

void main() {
	gl_Position = MVP * vec4(inPosition, 1.0f);
	outNormal = inNormal;
	outTexcoord0 = inTexcoord0;
}
