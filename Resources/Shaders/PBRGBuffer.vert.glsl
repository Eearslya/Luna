#version 460 core

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

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

struct Object {
	mat4 Transform;
	PositionData Positions;
	VertexData Vertices;
	uint MaterialIndex;
};

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

layout(set = 2, binding = 1, scalar) buffer readonly ObjectData {
	Object O[];
} Objects;

layout(location = 0) flat out uint outMaterialIndex;
layout(location = 1) out vec2 outUV0;
layout(location = 2) out mat3 outTBN;

invariant gl_Position;

void main() {
	Object obj = Objects.O[gl_InstanceIndex];

	vec3 inPosition = obj.Positions.P[gl_VertexIndex];
	Vertex inVertex = obj.Vertices.V[gl_VertexIndex];
	vec3 inNormal = inVertex.Normal;
	vec4 inTangent = inVertex.Tangent;
	vec2 inUV0 = inVertex.Texcoord0;

	mat4x3 worldTransform = mat4x3(obj.Transform);

	vec3 worldPosition = worldTransform * vec4(inPosition, 1.0f);
	gl_Position = Transform.ViewProjection * vec4(worldPosition, 1.0f);

	outMaterialIndex = obj.MaterialIndex;

	outUV0 = inUV0;

	mat3 normalTransform = inverse(transpose(mat3(worldTransform)));
	vec3 T = normalize(normalTransform * inTangent.xyz);
	vec3 B = normalize(normalTransform * (cross(inNormal.xyz, inTangent.xyz) * inTangent.w));
	vec3 N = normalize(normalTransform * inNormal.xyz);
	outTBN = mat3(T, B, N);
}
