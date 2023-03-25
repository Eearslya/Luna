#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV0;

layout(set = 0, binding = 0) uniform TransformData {
  mat4 Projection;
	mat4 View;
	mat4 ViewProjection;
	mat4 InvProjection;
	mat4 InvView;
	mat4 InvViewProjection;
	mat4 LocalViewProjection;
	mat4 InvLocalViewProjection;
	vec3 CameraPosition;
	vec3 CameraFront;
	vec3 CameraRight;
	vec3 CameraUp;
	float ZNear;
	float ZFar;
} Transform;

layout(push_constant) uniform PushConstant {
	mat4 Model;
} PC;

layout(location = 0) out vec2 outUV0;
layout(location = 1) out mat3 outTBN;

void main() {
	mat4x3 worldTransform = mat4x3(PC.Model);

	vec3 worldPosition = worldTransform * vec4(inPosition, 1.0f);
	gl_Position = Transform.ViewProjection * vec4(worldPosition, 1.0f);

	outUV0 = inUV0;

	mat3 normalTransform = mat3(worldTransform);
	vec3 T = normalize(normalTransform * inTangent.xyz);
	vec3 B = normalize(normalTransform * (cross(inNormal.xyz, inTangent.xyz) * inTangent.w));
	vec3 N = normalize(normalTransform * inNormal.xyz);
	outTBN = mat3(T, B, N);
}
