#version 450 core

struct LightData {
	vec4 Position;
	vec4 Color;
};

layout(location = 0) in vec2 inUV0;

layout(set = 0, binding = 0) uniform CameraData {
	mat4 Projection;
	mat4 View;
	mat4 ViewInverse;
	vec3 Position;
} Camera;

layout(set = 0, binding = 1) uniform SceneData {
	vec4 SunDirection;
	float PrefilteredCubeMipLevels;
	float Exposure;
	float Gamma;
	float IBLContribution;
} Scene;

layout(set = 0, binding = 2) uniform LightsData {
	LightData[32] Lights;
	int LightCount;
} Lights;

layout(set = 0, binding = 3) uniform samplerCube TexIrradiance;
layout(set = 0, binding = 4) uniform samplerCube TexPrefiltered;
layout(set = 0, binding = 5) uniform sampler2D TexBrdfLut;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput InputPosition;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput InputNormal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput InputAlbedo;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput InputPBR;
layout(input_attachment_index = 4, set = 1, binding = 4) uniform subpassInput InputEmissive;

layout(location = 0) out vec4 outColor;

const float PI = 3.141592653589793f;

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
	return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float NdotH = max(dot(N, H), 0.0);
	const float NdotH2 = NdotH * NdotH;

	const float num = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
	const float r = roughness + 1.0;
	const float k = (r * r) / 8.0;
	const float num = NdotV;
	const float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
	const float NdotV = max(dot(N, V), 0.0);
	const float NdotL = max(dot(N, L), 0.0);
	const float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	const float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

void main() {
	const vec3 WorldPos = subpassLoad(InputPosition).xyz;
	const vec3 N = subpassLoad(InputNormal).xyz;
	//const vec3 L = -normalize(Scene.SunDirection.xyz);
	const vec3 V = normalize(Camera.Position - WorldPos);
	//const float NdotL = clamp(dot(N, L), 0.001, 1.0);
	const vec4 BaseColor = subpassLoad(InputAlbedo);
	const vec3 PBR = subpassLoad(InputPBR).xyz;
	const vec3 Emissive = subpassLoad(InputEmissive).rgb;
	const float Metallic = PBR.b;
	const float Roughness = PBR.g;

	vec3 Lo = vec3(0.0);
	for (int i = 0; i < Lights.LightCount; ++i) {
		const vec3 L = normalize(Lights.Lights[i].Position.xyz - WorldPos);
		const vec3 H = normalize(V + L);
		const float distance = length(Lights.Lights[i].Position.xyz - WorldPos);
		const float attenuation = 1.0 / (distance * distance);
		const vec3 radiance = Lights.Lights[i].Color.rgb * attenuation;
		const vec3 f0 = mix(vec3(0.04), BaseColor.rgb, Metallic);
		const vec3 F = FresnelSchlick(max(dot(H, V), 0.0), f0);
		const float NDF = DistributionGGX(N, H, Roughness);
		const float G = GeometrySmith(N, V, L, Roughness);
		const vec3 num = NDF * G * F;
		const float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
		const vec3 specular = num / denom;
		const vec3 kS = F;
		const vec3 kD = (vec3(1.0) - kS) * (1.0 - Metallic);
		const float NdotL = max(dot(N, L), 0.0);

		Lo += (kD * BaseColor.rgb / PI + specular) * radiance * NdotL;
	}

	vec3 Albedo = (BaseColor.rgb * vec3(0.1)) + Lo;
	Albedo = Albedo / (Albedo + vec3(1.0));

	outColor = vec4(Albedo, BaseColor.a);
}
