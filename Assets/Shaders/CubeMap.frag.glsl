#version 450 core

layout(location = 0) in vec3 inLocalPos;

layout(set = 0, binding = 0) uniform sampler2D envMap;

layout(location = 0) out vec4 outColor;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
	return (vec2(atan(v.z, v.x), asin(v.y)) * invAtan) + vec2(0.5);
}

void main() {
	const vec2 uv = SampleSphericalMap(normalize(inLocalPos));
	outColor = vec4(texture(envMap, uv).rgb, 1.0);
}
