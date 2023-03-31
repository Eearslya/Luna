#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

#include "Common.glsl"
#include "Normal.glsli"
#include "Srgb.glsli"

layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput Albedo;
layout(set = 0, binding = 1, input_attachment_index = 1) uniform subpassInput Normal;
layout(set = 0, binding = 2, input_attachment_index = 2) uniform subpassInput PBR;
layout(set = 0, binding = 3, input_attachment_index = 3) uniform subpassInput Depth;

layout(set = 1, binding = 0) uniform sampler2D Textures[];
layout(set = 1, binding = 0) uniform samplerCube CubeTextures[];

layout(push_constant) uniform LightingData {
	mat4 InvViewProjection;
	vec3 CameraPosition;
	float IBLStrength;
	vec2 InvResolution;
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
	float LdotH;
	float NdotH;
	float NdotL;
	float NdotV;
	float VdotH;
	float Metallic;
	float Roughness;
	float AlphaRoughness;
} PBRInfo;

vec3 SpecularReflection() {
	return PBRInfo.Reflectance0 + (PBRInfo.Reflectance90 - PBRInfo.Reflectance0) * pow(clamp(1.0 - PBRInfo.VdotH, 0.0, 1.0), 5.0);
}

float GeometricOcclusion() {
	float r = PBRInfo.AlphaRoughness * PBRInfo.AlphaRoughness;
	float attenuationL = 2.0 * PBRInfo.NdotL / (PBRInfo.NdotL + sqrt(r + (1.0 - r) * (PBRInfo.NdotL * PBRInfo.NdotL)));
	float attenuationV = 2.0 * PBRInfo.NdotV / (PBRInfo.NdotV + sqrt(r + (1.0 - r) * (PBRInfo.NdotV * PBRInfo.NdotV)));

	return attenuationL * attenuationV;
}

float MicrofacetDistribution() {
	float roughnessSq = PBRInfo.AlphaRoughness * PBRInfo.AlphaRoughness;
	float f = (PBRInfo.NdotH * roughnessSq - PBRInfo.NdotH) * PBRInfo.NdotH + 1.0;

	return roughnessSq / (Pi * f * f);
}

vec3 GetIBLContribution() {
	float lod = (PBRInfo.Roughness * Lighting.PrefilterMipLevels);
	vec3 brdf = (texture(Textures[nonuniformEXT(Lighting.Brdf)], vec2(PBRInfo.NdotV, 1.0 - PBRInfo.Roughness))).rgb;
	vec3 diffuseLight = SrgbToLinear(textureLod(CubeTextures[nonuniformEXT(Lighting.Irradiance)], PBRInfo.N, 0)).rgb;
	vec3 specularLight = SrgbToLinear(textureLod(CubeTextures[nonuniformEXT(Lighting.Prefilter)], PBRInfo.Reflection, lod)).rgb;
	vec3 diffuse = (diffuseLight * PBRInfo.DiffuseColor) * Lighting.IBLStrength;
	vec3 specular = (specularLight * (PBRInfo.SpecularColor * brdf.x + brdf.y)) * Lighting.IBLStrength;

	return diffuse + specular;
}

vec3 FresnelSchlickRoughness(vec3 f0, float cosTheta, float roughness) {
	return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - cosTheta, 5.0);
}

vec3 CalculateImageBasedLighting() {
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

	vec4 clipPosition = Lighting.InvViewProjection * vec4(2.0 * gl_FragCoord.xy * Lighting.InvResolution - 1.0, depth, 1.0);
	vec3 position = clipPosition.xyz / clipPosition.w;

	vec4 baseColor = albedo;
	float metallic = orm.b;
	float roughness = orm.g;
	PBRInfo.N = normal;
	PBRInfo.V = normalize(Lighting.CameraPosition.xyz - position);
	vec3 L = normalize(vec3(10, 10, 10));
	vec3 H = normalize(L + PBRInfo.V);

	vec3 lightColor = vec3(1);
	vec3 f0 = vec3(0.04);

	PBRInfo.DiffuseColor = (baseColor.rgb * (vec3(1.0) - f0)) * (1.0 - metallic);
	PBRInfo.SpecularColor = mix(f0, baseColor.rgb, metallic);
	PBRInfo.Reflectance0 = PBRInfo.SpecularColor;
	PBRInfo.Reflectance90 = vec3(1) * clamp(max(max(PBRInfo.SpecularColor.r, PBRInfo.SpecularColor.g), PBRInfo.SpecularColor.b) * 25.0, 0.0, 1.0);
	PBRInfo.Reflection = -normalize(reflect(PBRInfo.V, PBRInfo.N));
	PBRInfo.LdotH = clamp(dot(L, H), 0.0, 1.0);
	PBRInfo.NdotH = clamp(dot(PBRInfo.N, H), 0.0, 1.0);
	PBRInfo.NdotL = clamp(dot(PBRInfo.N, L), 0.001, 1.0);
	PBRInfo.NdotV = clamp(abs(dot(PBRInfo.N, PBRInfo.V)), 0.001, 1.0);
	PBRInfo.VdotH = clamp(dot(PBRInfo.V, H), 0.0, 1.0);
	PBRInfo.Metallic = metallic;
	PBRInfo.Roughness = roughness;
	PBRInfo.AlphaRoughness = roughness * roughness;

	vec3 F = SpecularReflection();
	float G = GeometricOcclusion();
	float D = MicrofacetDistribution();

	vec3 diffuseContrib = (1.0 - F) * (PBRInfo.DiffuseColor / Pi);
	vec3 specularContrib = F * G * D / (4.0 * PBRInfo.NdotL * PBRInfo.NdotV);
	vec3 iblContrib = GetIBLContribution();

	vec3 lightContribution = vec3(0);
	vec3 iblContribution = CalculateImageBasedLighting();

	// vec3 color = (PBRInfo.NdotL * lightColor * (diffuseContrib + specularContrib)) + iblContrib;
	vec3 color = lightContribution + iblContribution;

	outColor = vec4(color, baseColor.a);

	// outColor = SrgbToLinear(vec4(position, 1.0f));
	// outColor = vec4(iblContrib, baseColor.a);
	// outColor = vec4(vec3(D), 1.0);
	// outColor = SrgbToLinear(vec4(PBRInfo.N * 0.5f + 0.5f, 1.0f));
}
