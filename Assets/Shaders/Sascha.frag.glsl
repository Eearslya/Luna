#version 450 core

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;

layout(set = 0, binding = 0) uniform CameraData {
	mat4 Projection;
	mat4 View;
	vec3 Position;
} Camera;

layout(set = 0, binding = 1) uniform SceneData {
	vec4 SunDirection;
} Scene;

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
	vec3  Reflectance0;
	vec3  Reflectance90;
	float AlphaRoughness;
	vec3  DiffuseColor;
	vec3  SpecularColor;
};

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

vec3 Diffuse(PBRInfo pbr) {
	return pbr.DiffuseColor / PI;
}

void main() {
	// Load Albedo color.
	vec4 baseColor;
	if (Material.HasAlbedo == 1) {
		baseColor = SrgbToLinear(texture(texAlbedo, inUV0)) * Material.BaseColorFactor;
	} else {
		baseColor = Material.BaseColorFactor;
	}

	// If the material uses alpha masking, check if we pass the test.
	if (Material.AlphaMask == 1.0f) {
		if (baseColor.a < Material.AlphaCutoff) { discard; }
	}

	// Load our PBR metallic/roughness information.
	float perceptualRoughness = Material.Roughness;
	float metallic = Material.Metallic;
	if (Material.HasPBR == 1) {
		vec4 pbrSample = texture(texPBR, inUV0);
		perceptualRoughness = pbrSample.g;
		metallic = pbrSample.b;
	} else {
		perceptualRoughness = clamp(perceptualRoughness, MinRoughness, 1.0);
		metallic = clamp(metallic, 0.0, 1.0);
	}

	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;

	const float alphaRoughness = perceptualRoughness * perceptualRoughness;
	const vec3 specularColor = mix(f0, baseColor.rgb, metallic);
	const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	const float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	const vec3 specularEnvironmentR0 = specularColor.rgb;
	const vec3 specularEnvironmentR90 = vec3(1.0) * reflectance90;
	const vec3 n = (Material.HasNormal == 1) ? GetNormal() : normalize(inNormal);
	const vec3 v = normalize(Camera.Position - inWorldPos);
	//const vec3 l = normalize(Scene.SunDirection.xyz);
	const vec3 l = normalize(vec3(0, 1, 0));
	const vec3 h = normalize(l + v);
	vec3 reflection = -normalize(reflect(v, n));
	reflection.y *= -1.0f;

	const float NdotL = clamp(dot(n, l), 0.001, 1.0);
	const float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	const float NdotH = clamp(dot(n, h), 0.0, 1.0);
	const float LdotH = clamp(dot(l, h), 0.0, 1.0);
	const float VdotH = clamp(dot(v, h), 0.0, 1.0);

	const PBRInfo pbr = PBRInfo(
		NdotL,
		NdotV,
		NdotH,
		LdotH,
		VdotH,
		perceptualRoughness,
		metallic,
		specularEnvironmentR0,
		specularEnvironmentR90,
		alphaRoughness,
		diffuseColor,
		specularColor
	);

	const vec3 F = SpecularReflection(pbr);
	const float G = GeometricOcclusion(pbr);
	const float D = MicrofacetDistribution(pbr);

	const vec3 SunColor = vec3(1.0);

	const vec3 diffuseContrib = (1.0 - F) * Diffuse(pbr);
	const vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
	vec3 color = NdotL * SunColor * (diffuseContrib + specContrib);

	//outColor = vec4(metallic, metallic, metallic, 1.0f);
	//outColor = vec4(perceptualRoughness, perceptualRoughness, perceptualRoughness, 1.0f);
	outColor = vec4(color, baseColor.a);
	//const float debug = v; outColor = vec4(debug, debug, debug, 1.0);
	//const vec3 debug = specularColor; outColor = vec4(debug, 1.0);
}
