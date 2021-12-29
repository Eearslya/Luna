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

//#define SRGB_FAST

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

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
	return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 n, vec3 h, float roughness) {
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float NdotH = max(dot(n, h), 0.0);
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

float GeometrySmith(vec3 n, vec3 v, vec3 l, float roughness) {
	const float NdotV = max(dot(n, v), 0.0);
	const float NdotL = max(dot(n, l), 0.0);
	const float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	const float ggx1 = GeometrySchlickGGX(NdotL, roughness);
	return ggx1 * ggx2;
}

void main() {
	const vec4 baseColor = SrgbToLinear(texture(texAlbedo, inUV0) * Material.BaseColorFactor);

	if (Material.AlphaMask == 1) {
		if (baseColor.a < Material.AlphaCutoff) { discard; }
	}

	const vec3 albedo = baseColor.rgb;
	float metallic = Material.Metallic;
	float roughness = Material.Roughness;

	if (Material.HasPBR == 1) {
		const vec4 pbrSample = texture(texPBR, inUV0);
		metallic = pbrSample.b;
		roughness = pbrSample.g;
	}

	const vec3 n = Material.HasNormal == 1 ? GetNormal() : normalize(inNormal);
	const vec3 v = normalize(Camera.Position - inWorldPos);

	vec3 f0 = vec3(0.04);
	f0 = mix(f0, albedo, metallic);

	vec3 Lo = vec3(0.0);
	//const vec3 l = normalize(Scene.SunDirection.xyz);
	const vec3 l = normalize(vec3(2, 2, 1));
	const vec3 h = normalize(v + l);
	const vec3 radiance = vec3(5.0);
	const float NDF = DistributionGGX(n, h, roughness);
	const float G = GeometrySmith(n, v, l, roughness);
	const vec3 F = FresnelSchlick(max(dot(h, v), 0.0), f0);
	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic;
	vec3 numerator = NDF * G * F;
	float denominator = 4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0) + 0.0001;
	vec3 specular = numerator / denominator;
	const float NdotL = max(dot(n, l), 0.0);
	Lo += (kD * albedo / PI + specular) * radiance * NdotL;

	const vec3 ambient = vec3(0.03) * albedo;
	vec3 color = ambient + Lo;
	color = color / (color + vec3(1.0));

	//outColor = vec4(metallic, metallic, metallic, 1.0f);
	//outColor = vec4(perceptualRoughness, perceptualRoughness, perceptualRoughness, 1.0f);
	outColor = vec4(color, baseColor.a);
	//const float debug = v; outColor = vec4(debug, debug, debug, 1.0);
	//const vec3 debug = n; outColor = vec4(debug, 1.0);
}
