#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstant {
	mat4 MVP;
};

const vec3 Positions[3] = vec3[3](
	vec3(-1.0f, -1.0f, 0.0f),
	vec3(1.0f, -1.0f, 0.0f),
	vec3(0.0f, 1.0f, 0.0f)
);

void main() {
	gl_Position = MVP * vec4(inPosition, 1.0f);
}
