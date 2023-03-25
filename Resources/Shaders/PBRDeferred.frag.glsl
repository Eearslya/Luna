#version 460 core

#include "Normal.glsli"
#include "Srgb.glsli"

layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput Albedo;
layout(set = 0, binding = 1, input_attachment_index = 1) uniform subpassInput Normal;

layout(location = 0) out vec4 outColor;

void main() {
	vec4 albedo = subpassLoad(Albedo);
	vec2 normalEncoded = subpassLoad(Normal).xy;
	vec3 normal = DecodeNormal(normalEncoded);

	vec3 L = normalize(vec3(10, 10, 10));
	float NdotL = max(dot(normal, L), 0.0);
	outColor = vec4(albedo.rgb * NdotL, 1.0);

	//outColor = SrgbToLinear(vec4(normal * 0.5f + 0.5f, 1.0f));
}
