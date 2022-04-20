#version 450 core

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inViewPos;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV0;

layout(set = 1, binding = 0) uniform MaterialData {
	vec4 BaseColorFactor;
	vec4 EmissiveFactor;
	int HasAlbedo;
	int HasNormal;
	int HasPBR;
	int HasEmissive;
	float AlphaMask;
	float AlphaCutoff;
	float Metallic;
	float Roughness;
	float DebugView;
} Material;

layout(set = 1, binding = 1) uniform sampler2D texAlbedo;
layout(set = 1, binding = 2) uniform sampler2D texNormal;
layout(set = 1, binding = 3) uniform sampler2D texPBR;
layout(set = 1, binding = 4) uniform sampler2D texEmissive;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outPBR;
layout(location = 4) out vec4 outEmissive;

const float PI = 3.141592653589793f;
const float MinRoughness = 0.04f;

vec3 GetNormal() {
	const vec3 tangentNormal = texture(texNormal, inUV0).xyz * 2.0 - 1.0;
	const vec3 q1 = dFdx(inWorldPos);
	const vec3 q2 = dFdy(inWorldPos);
	const vec2 st1 = dFdx(inUV0);
	const vec2 st2 = dFdy(inUV0);
	const vec3 N = normalize(inNormal);
	const vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	const vec3 B = -normalize(cross(N, T));
	const mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

void main() {
	vec4 baseColor = Material.BaseColorFactor;
	if (Material.HasAlbedo == 1) {
		baseColor = texture(texAlbedo, inUV0) * baseColor;
	}
	if (Material.AlphaMask == 1 && baseColor.a < Material.AlphaCutoff) { discard; }

	float metallic = Material.Metallic;
	float roughness = Material.Roughness;
	if (Material.HasPBR == 1) {
		const vec4 pbrSample = texture(texPBR, inUV0);
		metallic = pbrSample.b * metallic;
		roughness = pbrSample.g * roughness;
	} else {
		metallic = clamp(metallic, 0.0, 1.0);
		roughness = clamp(roughness, MinRoughness, 1.0);
	}

	vec3 emissive = vec3(0);
	if (Material.HasEmissive == 1) {
		emissive = vec3(texture(texEmissive, inUV0) * Material.EmissiveFactor);
	}

	outPosition = vec4(inWorldPos, 0.0f);
	outNormal = vec4(Material.HasNormal == 1 ? GetNormal() : normalize(inNormal), 1.0f);
	outAlbedo = baseColor;
	outPBR = vec4(0.0f, roughness, metallic, 0.0f);
	outEmissive = vec4(emissive, 0.0f);
}
