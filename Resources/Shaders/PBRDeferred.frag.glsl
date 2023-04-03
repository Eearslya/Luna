#version 460 core

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsl"
#include "Normal.glsli"
#include "Srgb.glsli"

struct PointLight {
	vec3 Position;
	float Multiplier;
	vec3 Radiance;
	float MinRadius;
	float Radius;
	float Falloff;
	float LightSize;
};

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0, scalar) uniform PointLightData {
	uint Count;
	PointLight L[1024];
} Point;
layout(set = 0, binding = 1, input_attachment_index = 0) uniform subpassInput Albedo;
layout(set = 0, binding = 2, input_attachment_index = 1) uniform subpassInput Normal;
layout(set = 0, binding = 3, input_attachment_index = 2) uniform subpassInput PBR;
layout(set = 0, binding = 4, input_attachment_index = 3) uniform subpassInput Depth;

layout(set = 1, binding = 0) uniform sampler2D Textures[];
layout(set = 1, binding = 0) uniform samplerCube CubeTextures[];

layout(push_constant) uniform LightingData {
	mat4 InvViewProjection;
	vec3 CameraPosition;
	float IBLStrength;
	float PrefilterMipLevels;
	uint Irradiance;
	uint Prefilter;
	uint Brdf;
} Lighting;

layout(location = 0) out vec4 outColor;

struct PBRData {
	vec3 DiffuseColor;
	vec3 SpecularColor;
	vec3 Reflectance0;
	vec3 Reflectance90;
	vec3 Reflection;
	vec3 N;
	vec3 V;
	float NdotV;
	float Metallic;
	float Roughness;
	float AlphaRoughness;
} PBRInfo;

vec3 FresnelSchlickRoughness(vec3 f0, float cosTheta, float roughness) {
	return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - cosTheta, 5.0);
}

float GaSchlickG1(float cosTheta, float k) {
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float GaSchlickGGX(float cosLi, float NdotV, float roughness) {
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;

	return GaSchlickG1(cosLi, k) * GaSchlickG1(NdotV, k);
}

float NdfGGX(float cosLh, float alphaRoughness) {
	float alphaSq = alphaRoughness * alphaRoughness;
	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;

	return alphaSq / (Pi * denom * denom);
}

vec3 CalculatePointLights(vec3 worldPos) {
	vec3 result = vec3(0);
	for (uint i = 0; i < Point.Count; ++i) {
		PointLight point = Point.L[i];

		vec3 Li = normalize(point.Position - worldPos);
		float lightDistance = length(point.Position - worldPos);
		vec3 Lh = normalize(Li + PBRInfo.V);

		float attenuation = clamp(1.0 - (lightDistance * lightDistance) / (point.Radius * point.Radius), 0.0, 1.0);
		attenuation *= mix(attenuation, 1.0, point.Falloff);

		vec3 Lradiance = point.Radiance * point.Multiplier * attenuation;
		float cosLi = max(0.0, dot(PBRInfo.N, Li));
		float cosLh = max(0.0, dot(PBRInfo.N, Lh));

		vec3 F = FresnelSchlickRoughness(Fdielectric, max(0.0, dot(Lh, PBRInfo.V)), PBRInfo.Roughness);
		float D = NdfGGX(cosLh, PBRInfo.AlphaRoughness);
		float G = GaSchlickGGX(cosLi, PBRInfo.NdotV, PBRInfo.Roughness);

		vec3 kd = (1.0 - F) * (1.0 - PBRInfo.Metallic);
		vec3 diffuseBRDF = kd * PBRInfo.DiffuseColor;

		vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * PBRInfo.NdotV);
		specularBRDF = clamp(specularBRDF, vec3(0), vec3(10));

		result += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
	}

	return result;
}

vec3 CalculateImageBasedLighting() {
	if (Lighting.IBLStrength == 0.0) { return vec3(0); }

	vec3 irradiance = textureLod(CubeTextures[nonuniformEXT(Lighting.Irradiance)], PBRInfo.N, 0).rgb;
	vec3 diffuseIBL = PBRInfo.DiffuseColor * irradiance;

	int irradianceMips = textureQueryLevels(CubeTextures[nonuniformEXT(Lighting.Irradiance)]);
	vec3 specularIrradiance = textureLod(CubeTextures[nonuniformEXT(Lighting.Prefilter)], PBRInfo.Reflection, PBRInfo.Roughness * irradianceMips).rgb;
	vec2 specularBRDF = texture(Textures[nonuniformEXT(Lighting.Brdf)], vec2(PBRInfo.NdotV, PBRInfo.Roughness)).rg;
	vec3 specularIBL = specularIrradiance * (PBRInfo.SpecularColor * specularBRDF.x + specularBRDF.y);

	vec3 F = FresnelSchlickRoughness(Fdielectric, PBRInfo.NdotV, PBRInfo.Roughness);
	vec3 kd = (1.0 - F) * (1.0 - PBRInfo.Metallic);

	return kd * diffuseIBL + specularIBL;
}

void main() {
	vec4 albedo = subpassLoad(Albedo);
	vec2 normalEncoded = subpassLoad(Normal).xy;
	vec3 normal = DecodeNormal(normalEncoded);
	vec3 orm = subpassLoad(PBR).xyz;
	float depth = subpassLoad(Depth).r;

	vec4 clipPosition = Lighting.InvViewProjection * vec4(inUV * 2.0 - 1.0, depth, 1.0);
	vec3 position = clipPosition.xyz / clipPosition.w;

	float metallic = orm.b;
	float roughness = orm.g;
	PBRInfo.N = normal;
	PBRInfo.V = normalize(Lighting.CameraPosition.xyz - position);

	PBRInfo.DiffuseColor = (albedo.rgb * (vec3(1.0) - Fdielectric)) * (1.0 - metallic);
	PBRInfo.SpecularColor = mix(Fdielectric, albedo.rgb, metallic);
	PBRInfo.Reflectance0 = PBRInfo.SpecularColor;
	PBRInfo.Reflectance90 = vec3(1) * clamp(max(max(PBRInfo.SpecularColor.r, PBRInfo.SpecularColor.g), PBRInfo.SpecularColor.b) * 25.0, 0.0, 1.0);
	PBRInfo.Reflection = -normalize(reflect(PBRInfo.V, PBRInfo.N));
	PBRInfo.NdotV = clamp(abs(dot(PBRInfo.N, PBRInfo.V)), 0.001, 1.0);
	PBRInfo.Metallic = metallic;
	PBRInfo.Roughness = roughness;
	PBRInfo.AlphaRoughness = roughness * roughness;

	vec3 lightContribution = CalculatePointLights(position);
	vec3 iblContribution = CalculateImageBasedLighting() * Lighting.IBLStrength;

	vec3 color = lightContribution + iblContribution;

	outColor = vec4(color, albedo.a);

	// outColor = SrgbToLinear(vec4(position, 1.0f));
	// outColor = vec4(lightContribution, baseColor.a);
	// outColor = vec4(vec3(D), 1.0);
	// outColor = SrgbToLinear(vec4(PBRInfo.N * 0.5f + 0.5f, 1.0f));
	// outColor = vec4(position, 1);
}
