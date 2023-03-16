#version 460 core

const float Epsilon = 0.00001;
const float Pi = 3.141592;
const int ShadowCascadeCount = 4;
const float TwoPi = 2 * Pi;

layout(set = 0, binding = 0) uniform SceneUBO {
  mat4 Projection;
  mat4 View;
	mat4 ViewProjection;
	vec4 ViewPosition;
	vec4 SunPosition;
	float Exposure;
	float Gamma;
	float PrefilteredMipLevels;
	float IBLStrength;
} Scene;
layout(set = 0, binding = 1) uniform samplerCube TexIrradiance;
layout(set = 0, binding = 2) uniform samplerCube TexPrefilter;
layout(set = 0, binding = 3) uniform sampler2D TexBrdf;

layout(set = 1, binding = 0) uniform subpassInput InAlbedo;
layout(set = 1, binding = 1) uniform subpassInput InNormal;
layout(set = 1, binding = 2) uniform subpassInput InORM;
layout(set = 1, binding = 3) uniform subpassInput InEmissive;

layout(location = 0) out vec4 outColor;

struct PBRInfo {
	vec3 DiffuseColor;
	vec3 SpecularColor;
	vec3 Reflectance0;
	vec3 Reflectance90;
	vec3 Reflection;
	vec3 N;
	float LdotH;
	float NdotH;
	float NdotL;
	float NdotV;
	float VdotH;
	float Metallic;
	float Roughness;
	float AlphaRoughness;
} PBR;

vec4 SrgbToLinear(vec4 srgb) {
	vec3 bLess = step(vec3(0.04045), srgb.xyz);
	vec3 linear = mix(srgb.xyz / vec3(12.92), pow((srgb.xyz + vec3(0.055)) / vec3(1.055), vec3(2.4)), bLess);
	return vec4(linear, srgb.a);
}

vec3 Uncharted2Tonemap(vec3 c) {
	const float A = 0.15f;
	const float B = 0.50f;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.02f;
	const float F = 0.30f;
	const float W = 11.2f;

	return ((c * (A * c + C * B) + D * E) / (c * (A * c + B) + D * F)) - E / F;
}

vec4 Tonemap(vec4 color) {
	vec3 c = Uncharted2Tonemap(color.rgb * Scene.Exposure);
	c = c * (1.0f / Uncharted2Tonemap(vec3(11.2f)));

	return vec4(pow(c, vec3(1.0f / Scene.Gamma)), color.a);
}

float GeometricOcclusion() {
	float r = PBR.AlphaRoughness * PBR.AlphaRoughness;
	float attenuationL = 2.0 * PBR.NdotL / (PBR.NdotL + sqrt(r + (1.0 - r) * (PBR.NdotL * PBR.NdotL)));
	float attenuationV = 2.0 * PBR.NdotV / (PBR.NdotV + sqrt(r + (1.0 - r) * (PBR.NdotV * PBR.NdotV)));

	return attenuationL * attenuationV;
}

vec3 GetIBLContribution() {
	float lod = (PBR.Roughness * Scene.PrefilteredMipLevels);
	vec3 brdf = (texture(TexBrdf, vec2(PBR.NdotV, 1.0 - PBR.Roughness))).rgb;
	vec3 diffuseLight = SrgbToLinear(Tonemap(texture(TexIrradiance, PBR.N))).rgb;
	vec3 specularLight = SrgbToLinear(Tonemap(textureLod(TexPrefilter, PBR.Reflection, lod))).rgb;
	vec3 diffuse = (diffuseLight * PBR.DiffuseColor) * Scene.IBLStrength;
	vec3 specular = (specularLight * (PBR.SpecularColor * brdf.x + brdf.y)) * Scene.IBLStrength;

	// return PBR.N + 1.0 * 0.5;
	// return texture(TexIrradiance, PBR.N).rgb;
	// return textureLod(TexIrradiance, PBR.N, 0).rgb;
	// return PBR.Reflection + 1.0 * 0.5;
	// return textureLod(TexPrefilter, PBR.Reflection, 0).rgb;
	// return diffuseLight;
	return diffuse + specular;
}

float MicrofacetDistribution() {
	float roughnessSq = PBR.AlphaRoughness * PBR.AlphaRoughness;
	float f = (PBR.NdotH * roughnessSq - PBR.NdotH) * PBR.NdotH + 1.0;

	return roughnessSq / (Pi * f * f);
}

vec3 SpecularReflection() {
	return PBR.Reflectance0 + (PBR.Reflectance90 - PBR.Reflectance0) * pow(clamp(1.0 - PBR.VdotH, 0.0, 1.0), 5.0);
}

void main() {
	vec4 baseColor = subpassLoad(InAlbedo);
	PBR.N = subpassLoad(InNormal).xyz;
	vec3 orm = subpassLoad(InORM).xyz;
	vec3 emission = subpassLoad(InEmissive).rgb;

	float occlusion = orm.r;
	float roughness = orm.g;
	float metallic = orm.b;

	vec3 V = normalize(Scene.ViewPosition.xyz - In.WorldPos);
	vec3 L = normalize(Scene.SunPosition.xyz);
	vec3 H = normalize(L + V);

	vec3 lightColor = vec3(1);
	vec3 f0 = vec3(0.04);

	PBR.DiffuseColor = (baseColor.rgb * (vec3(1.0) - f0)) * (1.0 - metallic);
	PBR.SpecularColor = mix(f0, baseColor.rgb, metallic);
	PBR.Reflectance0 = PBR.SpecularColor;
	PBR.Reflectance90 = vec3(1) * clamp(max(max(PBR.SpecularColor.r, PBR.SpecularColor.g), PBR.SpecularColor.b) * 25.0, 0.0, 1.0);
	PBR.Reflection = -normalize(reflect(V, PBR.N));
	PBR.LdotH = clamp(dot(L, H), 0.0, 1.0);
	PBR.NdotH = clamp(dot(PBR.N, H), 0.0, 1.0);
	PBR.NdotL = clamp(dot(PBR.N, L), 0.001, 1.0);
	PBR.NdotV = clamp(abs(dot(PBR.N, V)), 0.001, 1.0);
	PBR.VdotH = clamp(dot(V, H), 0.0, 1.0);
	PBR.Metallic = metallic;
	PBR.Roughness = roughness;
	PBR.AlphaRoughness = roughness * roughness;

	vec3 F = SpecularReflection();
	float G = GeometricOcclusion();
	float D = MicrofacetDistribution();

	vec3 diffuseContrib = (1.0 - F) * (PBR.DiffuseColor / Pi);
	vec3 specularContrib = F * G * D / (4.0 * PBR.NdotL * PBR.NdotV);
	vec3 iblContrib = GetIBLContribution();
	vec3 color = (PBR.NdotL * lightColor * (diffuseContrib + specularContrib)) + iblContrib;

	color.rgb += emission;

  outColor = vec4(color, baseColor.a);
}
