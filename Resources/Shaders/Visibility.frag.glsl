#version 460 core

layout(push_constant) uniform PushConstant {
	mat4 Model;
	uint ObjectId;
	uint Masked;
} PC;

layout(location = 0) out uvec4 outVisibility;

void main() {
	uint mask = PC.Masked;
	uint objectId = PC.ObjectId & 0xff;
	uint triangleId = uint(gl_PrimitiveID) & 0x7fffff;
	uint vis = (mask << 31) | (objectId << 23) | triangleId;

	outVisibility = uvec4(vis, 0, 0, 0);
}
