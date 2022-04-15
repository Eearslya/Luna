#version 450 core

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

layout(set = 0, binding = 2) uniform samplerCube TexIrradiance;
layout(set = 0, binding = 3) uniform samplerCube TexPrefiltered;
layout(set = 0, binding = 4) uniform sampler2D TexBrdfLut;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput InputPosition;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput InputNormal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput InputAlbedo;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput InputPBR;
layout(input_attachment_index = 4, set = 1, binding = 4) uniform subpassInput InputEmissive;

layout(location = 0) out vec4 outColor;

const float PI = 3.141592653589793f;

struct PBRInfo {
  float NdotL;
	float NdotV;
	float NdotH;
	float LdotH;
	float VdotH;
	float PerceptualRoughness;
	float Metalness;
	vec3 Reflectance0;
	vec3 Reflectance90;
	float AlphaRoughness;
	vec3 DiffuseColor;
	vec3 SpecularColor;
};

vec3 TonemapUncharted2(vec3 color) {
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	const float W = 11.2;
	return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

vec4 Tonemap(vec4 color) {
  vec3 outColor = TonemapUncharted2(color.rgb * Scene.Exposure);
	outColor = outColor * (1.0f / TonemapUncharted2(vec3(11.2f)));
	return vec4(pow(outColor, vec3(1.0f / Scene.Gamma)), color.a);
}

vec4 SrgbToLinear(vec4 srgb) {
#ifdef SRGB_FAST
	const vec3 linear = pow(srgb.rgb, vec3(2.2));
#else
	const vec3 bLess = step(vec3(0.04045), srgb.rgb);
	const vec3 linear = mix(srgb.rgb / vec3(12.92), pow((srgb.rgb + vec3(0.055)) / vec3(1.055), vec3(2.4)), bLess);
#endif

	return vec4(linear, srgb.a);
}

vec3 Diffuse(PBRInfo pbr) {
  return pbr.DiffuseColor / PI;
}

vec3 SpecularReflection(PBRInfo pbr) {
  return pbr.Reflectance0 + (pbr.Reflectance90 - pbr.Reflectance0) * pow(clamp(1.0 - pbr.VdotH, 0.0, 1.0), 5.0);
}

float GeometricOcclusion(PBRInfo pbr) {
  const float NdotL = pbr.NdotL;
	const float NdotV = pbr.NdotV;
	const float r = pbr.AlphaRoughness;

  const float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	const float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

float MicrofacetDistribution(PBRInfo pbr) {
  const float roughnessSq = pbr.AlphaRoughness * pbr.AlphaRoughness;
	const float f = (pbr.NdotH * roughnessSq - pbr.NdotH) * pbr.NdotH + 1.0;
	return roughnessSq / (PI * f * f);
}

void main() {
	const vec3 worldPos = subpassLoad(InputPosition).xyz;
	const vec3 n = subpassLoad(InputNormal).xyz;
	const vec4 baseColor = subpassLoad(InputAlbedo);
	const vec3 pbrParams = subpassLoad(InputPBR).xyz;
	const vec3 emissive = subpassLoad(InputEmissive).xyz;

	const vec3 albedo = baseColor.rgb;
	const float metallic = pbrParams.b;
	const float roughness = pbrParams.g;

	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = (baseColor.rgb * (vec3(1.0) - f0)) * (1.0 - metallic);
	float alphaRoughness = roughness * roughness;
	vec3 specularColor = mix(f0, baseColor.rgb, metallic);
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;
	const vec3 v = normalize(Camera.Position - worldPos);
	vec3 l = -normalize(Scene.SunDirection.xyz);
	vec3 h = normalize(l + v);
	vec3 reflection = -normalize(reflect(v, n));
	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);

	PBRInfo pbr = PBRInfo(
		NdotL,
		NdotV,
		NdotH,
		LdotH,
		VdotH,
		roughness,
		metallic,
		specularEnvironmentR0,
		specularEnvironmentR90,
		alphaRoughness,
		diffuseColor,
		specularColor);

	vec3 F = SpecularReflection(pbr);
	float G = GeometricOcclusion(pbr);
	float D = MicrofacetDistribution(pbr);

	const vec3 sunColor = vec3(1.0);

	vec3 diffuseContrib = (1.0 - F) * Diffuse(pbr);
	vec3 specularContrib = F * G * D / (4.0 * NdotL * NdotV);


	vec3 iblDiffuse = vec3(0.0);
	vec3 iblSpecular = vec3(0.0);
	{
		const float lod = pbr.PerceptualRoughness * Scene.PrefilteredCubeMipLevels;
		const vec2 brdf = (texture(TexBrdfLut, vec2(pbr.NdotV, 1.0 - pbr.PerceptualRoughness))).rg;
		//const vec3 diffuseLight = SrgbToLinear(Tonemap(texture(TexIrradiance, n))).rgb;
		const vec3 diffuseLight = SrgbToLinear(Tonemap(textureLod(TexIrradiance, n, 0.0))).rgb;
		const vec3 specularLight = SrgbToLinear(Tonemap(textureLod(TexPrefiltered, reflection, lod))).rgb;
		iblDiffuse = (diffuseLight * pbr.DiffuseColor) * Scene.IBLContribution;
		iblSpecular = (specularLight * (pbr.SpecularColor * brdf.x + brdf.y) * Scene.IBLContribution);
	}

	vec3 iblContrib = iblDiffuse + iblSpecular;
	vec3 color = (NdotL * sunColor * (diffuseContrib + specularContrib)) + iblContrib;
	color += emissive;

	outColor = vec4(color, baseColor.a);
}
