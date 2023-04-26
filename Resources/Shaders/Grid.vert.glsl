#version 460 core

layout(set = 0, binding = 0) uniform CameraData {
	mat4 ViewProjection;
	mat4 InvViewProjection;
	mat4 Projection;
	mat4 InvProjection;
	mat4 View;
	mat4 InvView;
	vec4 Position;
} Camera;

const vec3 GridPlane[6] = vec3[](
	vec3(1, 1, 0),
	vec3(-1, -1, 0),
	vec3(-1, 1, 0),
	vec3(-1, -1, 0),
	vec3(1, 1, 0),
	vec3(1, -1, 0)
);

layout(location = 0) out vec3 outNearPoint;
layout(location = 1) out vec3 outFarPoint;

vec3 UnprojectPoint(float x, float y, float z) {
	vec4 unprojectedPoint = Camera.InvViewProjection * vec4(x, y, z, 1.0f);
	return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
	vec3 p = GridPlane[gl_VertexIndex].xyz;
	outNearPoint = UnprojectPoint(p.x, p.y, 0.0f);
	outFarPoint = UnprojectPoint(p.x, p.y, 1.0f);

	gl_Position = vec4(p, 1.0f);
}
