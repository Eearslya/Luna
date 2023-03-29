#version 460 core

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

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

struct Vertex {
	vec3 Normal;
	vec4 Tangent;
	vec2 Texcoord0;
	vec2 Texcoord1;
	vec4 Color0;
	uvec4 Joints0;
	vec4 Weights0;
};

layout(buffer_reference, scalar) readonly buffer PositionData { vec3 P[]; };
layout(buffer_reference, scalar) readonly buffer VertexData { Vertex V[]; };

layout(push_constant) uniform PushConstant {
	mat4 Model;
	PositionData Positions;
	VertexData Vertices;
} PC;

layout(location = 0) out vec2 outUV0;
layout(location = 1) out mat3 outTBN;

void main() {
	vec3 inPosition = PC.Positions.P[gl_VertexIndex];
	Vertex inVertex = PC.Vertices.V[gl_VertexIndex];
	vec3 inNormal = inVertex.Normal;
	vec4 inTangent = inVertex.Tangent;
	vec2 inUV0 = inVertex.Texcoord0;

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
