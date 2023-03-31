#version 460 core

#include "Transform.glsli"

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstant {
	mat4 Model;
	uint ObjectId;
	uint Masked;
} PC;

void main() {
	gl_Position = Transform.ViewProjection * PC.Model * vec4(inPosition, 1.0f);
}
