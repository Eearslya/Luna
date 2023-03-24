#version 460 core

#include "Transform.glsli"

struct StaticSubmeshInstanceInfo {
	mat4 Model;
};

layout(set = 1, binding = 0, std140) uniform InstanceData {
	StaticSubmeshInstanceInfo Instance[256];
};

const vec2 vertices[3] = vec2[3](
	vec2(-0.5f, -0.5f),
	vec2(0.5f, -0.5f),
	vec2(0.0f, 0.5f)
);

void main() {
	gl_Position = Transform.ViewProjection * Instance[gl_InstanceIndex].Model * vec4(vertices[gl_VertexIndex], 0.0f, 1.0f);
}
