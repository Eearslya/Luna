#version 460 core
#extension GL_EXT_ray_tracing : enable

layout(set = 0, binding = 0) uniform accelerationStructureEXT TLAS;
layout(set = 0, binding = 1, rgba8) uniform image2D Image;
layout(set = 0, binding = 2) uniform CameraData {
	mat4 ViewInverse;
	mat4 ProjectionInverse;
} Camera;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main() {
	const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
	vec2 d = inUV * 2.0 - 1.0;

	vec4 origin = Camera.ViewInverse * vec4(0, 0, 0, 1);
	vec4 target = Camera.ProjectionInverse * vec4(d.x, d.y, 1, 1);
	vec4 direction = Camera.ViewInverse * vec4(normalize(target.xyz), 0);

	float tMin = 0.001;
	float tMax = 10000.0;

	hitValue = vec3(0.0);

	traceRayEXT(TLAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin.xyz, tMin, direction.xyz, tMax, 0);

	imageStore(Image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
}
