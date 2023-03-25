#version 460 core

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

void main() {}
