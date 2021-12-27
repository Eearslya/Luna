#version 450 core

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inTexCoord;

layout(set = 1, binding = 0) uniform MaterialData {
	float AlphaCutoff;
} Material;
layout(set = 1, binding = 1) uniform sampler2D texAlbedo;

layout(location = 0) out vec4 outColor;

vec4 SrgbToLinear(vec4 srgb) {
#ifdef SRGB_FAST
	const vec3 linear = pow(srgb.rgb, vec3(2.2));
#else
	const vec3 bLess = step(vec3(0.04045), srgb.rgb);
	const vec3 linear = mix(srgb.rgb / vec3(12.92), pow((srgb.rgb + vec3(0.055)) / vec3(1.055), vec3(2.4)), bLess);
#endif

	return vec4(linear, srgb.a);
}

void main() {
	outColor = texture(texAlbedo, inTexCoord);
	if (outColor.a < Material.AlphaCutoff) { discard; }
	outColor = SrgbToLinear(outColor);
}
