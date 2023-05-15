#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraData {
	mat4 ViewProjection;
	mat4 InvViewProjection;
	mat4 Projection;
	mat4 InvProjection;
	mat4 View;
	mat4 InvView;
	vec3 Position;
	float ZNear;
	float ZFar;
} Camera;

layout(location = 0) out vec4 outColor;

vec3 Unproject(vec3 p) {
	vec4 u = Camera.InvViewProjection * vec4(p, 1.0f);
	return u.xyz / u.w;
}

vec4 Grid(vec3 fragPos, float scale, bool drawAxes) {
	vec2 coord = fragPos.xz * scale;
	vec2 derivative = fwidth(coord);
	vec2 grid = abs(fract(coord - 0.5f) - 0.5f) / derivative;
	float line = min(grid.x, grid.y);
	float minX = min(derivative.x, 1.0f);
	float minZ = min(derivative.y, 1.0f);
	vec4 color = vec4(0.2f, 0.2f, 0.2f, 1.0f - min(line, 1.0f));

	if (drawAxes && fragPos.x > (-1.0f / scale) * minX && fragPos.x < (1.0f / scale) * minX) {
		color.z = 1.0f;
	}
	if (drawAxes && fragPos.z > (-1.0f / scale) * minZ && fragPos.z < (1.0f / scale) * minZ) {
		color.r = 1.0f;
	}

	return color;
}

void main() {
	vec2 clipPos = inUV * 2.0f - 1.0f;
	vec3 clipNear = Unproject(vec3(clipPos, 1.0f));
	vec3 clipFar = Unproject(vec3(clipPos, 0.000001f));

	float t = -clipNear.y / (clipFar.y - clipNear.y);
	if (t <= 0.0f) { discard; }

	vec3 fragPos = clipNear + t * (clipFar - clipNear);
	vec4 clipSpacePos = Camera.ViewProjection * vec4(fragPos.xyz, 1.0f);
	float clipSpaceDepth = clipSpacePos.z / clipSpacePos.w;
	gl_FragDepth = clipSpaceDepth;

	vec4 grid = Grid(fragPos, 1.0f, true);
	outColor = grid;

	const float fadeDistance = 25.0f;
	float linearDepth = Camera.ZNear / clipSpaceDepth;
	float fade = 1.0 - min(1.0, linearDepth / fadeDistance);
	outColor.a *= fade;
}
