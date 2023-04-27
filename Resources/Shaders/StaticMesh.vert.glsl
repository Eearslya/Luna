#version 460 core

#include "Transform.glsli"

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inTexcoord0;

layout(location = 0) out vec2 outTexcoord0;

#if !defined(RENDERER_DEPTH)
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;

layout(location = 1) out mat3 outTBN;
#endif

struct StaticSubmeshInstanceInfo {
	mat4 Model;
};

layout(set = 1, binding = 0, std140) uniform InstanceData {
	StaticSubmeshInstanceInfo Instance[128];
};

void main() {
#if SKINNED_MESH
#else
	mat4x3 worldTransform = mat4x3(Instance[gl_InstanceIndex].Model[0].xyz,
																 Instance[gl_InstanceIndex].Model[1].xyz,
																 Instance[gl_InstanceIndex].Model[2].xyz,
																 Instance[gl_InstanceIndex].Model[3].xyz);
#endif

	vec3 worldPosition = worldTransform * vec4(inPosition, 1.0f);
	gl_Position = Camera.ViewProjection * vec4(worldPosition, 1.0f);

	outTexcoord0 = inTexcoord0;

#if !defined(RENDERER_DEPTH)
	mat3 normalTransform = inverse(transpose(mat3(worldTransform)));
	vec3 T = normalize(normalTransform * inTangent.xyz);
	vec3 B = normalize(normalTransform * (cross(inNormal.xyz, inTangent.xyz) * inTangent.w));
	vec3 N = normalize(normalTransform * inNormal.xyz);
	outTBN = mat3(T, B, N);
#endif
}
