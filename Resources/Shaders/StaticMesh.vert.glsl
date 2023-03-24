#version 460 core

#include "Transform.glsli"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

struct StaticSubmeshInstanceInfo {
	mat4 Model;
};

layout(set = 1, binding = 0, std140) uniform InstanceData {
	StaticSubmeshInstanceInfo Instance[256];
};

layout(location = 0) out vec3 outNormal;

void main() {
#if SKINNED_MESH
#else
	mat4x3 worldTransform = mat4x3(Instance[gl_InstanceIndex].Model[0].xyz,
																 Instance[gl_InstanceIndex].Model[1].xyz,
																 Instance[gl_InstanceIndex].Model[2].xyz,
																 Instance[gl_InstanceIndex].Model[3].xyz);
#endif

	vec3 worldPosition = worldTransform * vec4(inPosition, 1.0f);
	gl_Position = Transform.ViewProjection * vec4(worldPosition, 1.0f);

	mat3 normalTransform = mat3(worldTransform[0], worldTransform[1], worldTransform[2]);
	outNormal = normalize(normalTransform * inNormal);
}
