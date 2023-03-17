#version 460 core
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

struct ObjectData {
	uint64_t VertexAddress;
	uint64_t IndexAddress;
};

struct Vertex {
	vec3 Position;
	vec3 Normal;
	vec4 Tangent;
	vec2 UV0;
	vec2 UV1;
	vec4 Color0;
	uvec4 Joints0;
	vec4 Weights0;
};

layout(buffer_reference, scalar) buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) buffer Indices { uvec3 i[]; };
layout(set = 1, binding = 0, scalar) buffer ObjectsData {
	ObjectData o;
} Objects;

layout(push_constant) uniform PushConstant {
	uint64_t VertexAddress;
	uint64_t IndexAddress;
} PC;

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attributes;

void main() {
	const vec3 barycentricCoords = vec3(1.0f - attributes.x - attributes.y, attributes.x, attributes.y);
	// ObjectData obj = Objects.o[gl_InstanceCustomIndexEXT];
	Vertices vertices = Vertices(PC.VertexAddress);
	Indices indices = Indices(PC.IndexAddress);

	uvec3 ind = indices.i[gl_PrimitiveID];
	Vertex v0 = vertices.v[ind.x];
	Vertex v1 = vertices.v[ind.y];
	Vertex v2 = vertices.v[ind.z];

	vec3 pos = v0.Position * barycentricCoords.x + v1.Position * barycentricCoords.y + v2.Position * barycentricCoords.z;
	vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));

	vec3 norm = v0.Normal * barycentricCoords.x + v1.Normal * barycentricCoords.y + v2.Normal * barycentricCoords.z;
	vec3 N = normalize(vec3(norm * gl_WorldToObjectEXT));

	const vec3 L = normalize(vec3(10, 10, 10));
	float NdotL = max(dot(N, L), 0.0);

	hitValue = vec3(NdotL, NdotL, NdotL);
}
