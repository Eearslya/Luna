#version 460 core

struct VertexOut {
	vec3 WorldPos;
	vec2 UV0;
	vec2 UV1;
	vec4 Color0;
	mat3 NormalMat;
};

layout(location = 0) in VertexOut In;

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

layout(set = 1, binding = 0) uniform MaterialData {
	mat4 AlbedoTransform;
	mat4 NormalTransform;
	mat4 PBRTransform;
	mat4 OcclusionTransform;
	mat4 EmissiveTransform;

	vec4 BaseColorFactor;
	vec4 EmissiveFactor;

	int AlbedoIndex;
	int NormalIndex;
	int PBRIndex;
	int OcclusionIndex;
	int EmissiveIndex;

	int AlbedoUV;
	int NormalUV;
	int PBRUV;
	int OcclusionUV;
	int EmissiveUV;
	bool DoubleSided;
	int AlphaMode;
	float AlphaCutoff;
	float MetallicFactor;
	float RoughnessFactor;
	float OcclusionFactor;
} Material;
layout(set = 1, binding = 1) uniform sampler2D TexAlbedo;
layout(set = 1, binding = 2) uniform sampler2D TexNormal;
layout(set = 1, binding = 3) uniform sampler2D TexPBR;
layout(set = 1, binding = 4) uniform sampler2D TexOcclusion;
layout(set = 1, binding = 5) uniform sampler2D TexEmissive;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outORM;
layout(location = 3) out vec3 outEmissive;

void main() {
	vec4 baseColor = Material.BaseColorFactor * In.Color0;
	if (Material.AlbedoUV >= 0) {
		vec2 uvAlbedo = (mat3(Material.AlbedoTransform) * vec3(Material.AlbedoUV == 0 ? In.UV0 : In.UV1, 1)).xy;
		baseColor *= texture(TexAlbedo, uvAlbedo);
	}
	if (Material.AlphaMode == 1 && baseColor.a < Material.AlphaCutoff) { discard; }

	float metallic = Material.MetallicFactor;
	float roughness = Material.RoughnessFactor;
	if (Material.PBRUV >= 0) {
		vec2 uvPBR = (mat3(Material.PBRTransform) * vec3(Material.PBRUV == 0 ? In.UV0 : In.UV1, 1)).xy;
		vec4 metalRough = texture(TexPBR, uvPBR);
		metallic *= metalRough.b;
		roughness *= metalRough.g;
	}
	metallic = clamp(metallic, 0.0, 1.0);
	roughness = clamp(roughness, 0.05, 1.0);

	vec3 N = normalize(In.NormalMat[2]);
	if (Material.NormalUV >= 0) {
		vec2 uvNormal = (mat3(Material.NormalTransform) * vec3(Material.NormalUV == 0 ? In.UV0 : In.UV1, 1)).xy;
		N = texture(TexNormal, uvNormal).rgb * 2.0f - 1.0f;
		N = normalize(In.NormalMat * N);
	}

	float occlusion = Material.OcclusionFactor;
	if (Material.OcclusionUV >= 0) {
		vec2 uvOcclusion = (mat3(Material.OcclusionTransform) * vec3(Material.OcclusionUV == 0 ? In.UV0 : In.UV1, 1)).xy;
		float occSample = texture(TexOcclusion, uvOcclusion).r;
		occlusion *= occSample;
	}

	vec3 emission = vec3(0);
	if (Material.EmissiveUV >= 0) {
		vec2 uvEmissive = (mat3(Material.EmissiveTransform) * vec3(Material.EmissiveUV == 0 ? In.UV0 : In.UV1, 1)).xy;
		emission = texture(TexEmissive, uvEmissive).rgb * Material.EmissiveFactor.rgb;
	}

	outAlbedo = baseColor;
	outNormal = N;
	outORM = vec3(occlusion, metallic, roughness);
	outEmissive = emission;
}
