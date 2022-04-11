#version 450 core

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inViewPos;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV0;

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

layout(location = 0) out vec4 outColor;

const float PI = 3.141592653589793f;
const float MinRoughness = 0.04f;

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

//#define SRGB_FAST

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
	vec4 baseColor = Material.BaseColorFactor;
	if (Material.HasAlbedo == 1) {
		baseColor = SrgbToLinear(texture(texAlbedo, inUV0)) * baseColor;
	}
	if (Material.AlphaMask == 1 && baseColor.a < Material.AlphaCutoff) { discard; }

	const vec3 albedo = baseColor.rgb;
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

	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = (baseColor.rgb * (vec3(1.0) - f0)) * (1.0 - metallic);
	float alphaRoughness = roughness * roughness;
	vec3 specularColor = mix(f0, baseColor.rgb, metallic);
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;
	const vec3 n = Material.HasNormal == 1 ? GetNormal() : normalize(inNormal);
	const vec3 v = normalize(Camera.Position - inWorldPos);
	vec3 l = normalize(Scene.SunDirection.xyz);
	vec3 h = normalize(l + v);
	vec3 reflection = -normalize(reflect(v, n));
	reflection.y *= -1.0f;
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

	outColor = vec4(color, baseColor.a);

	if (Material.DebugView > 0.0) {
		const int index = int(Material.DebugView);
		float v = 0.0;
		switch(index) {
			case 1:
				outColor.rgba = texture(texAlbedo, inUV0).rgba;
				break;
			case 2:
				outColor.rgb = texture(texNormal, inUV0).rgb;
				break;
			case 3:
				outColor.rgb = n;
				break;
			case 4:
				outColor.rgb = diffuseContrib;
				break;
			case 5:
				outColor.rgb = specularContrib;
				break;
			case 6:
				outColor.rgb = iblContrib;
				break;
		}
		outColor = SrgbToLinear(outColor);
	}
}