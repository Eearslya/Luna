#version 450 core

layout(location = 0) in vec3 inLocalPos;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(push_constant) uniform PushConstants {
	layout (offset = 64) float Roughness;
} PC;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float Random(vec2 co) {
	const float a = 12.9898;
	const float b = 78.233;
	const float c = 43758.5453;
	const float dt = dot(co.xy, vec2(a, b));
	const float sn = mod(dt, 3.14);
	return fract(sin(sn) * c);
}

vec2 Hammersley2D(uint i, uint N) {
  uint bits = (i << 16u) | (i >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
	bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
	bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
	const float rdi = float(bits) * 2.3283064365386963e-10;
	return vec2(float(i) / float(N), rdi);
}

vec3 ImportanceSampleGGX(vec2 Xi, float roughness, vec3 normal) {
  const float alpha = roughness * roughness;
	const float phi = 2.0 * PI * Xi.x + Random(normal.xz) * 0.1;
	const float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
	const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	const vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

  const vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	const vec3 tangentX = normalize(cross(up, normal));
	const vec3 tangentY = normalize(cross(normal, tangentX));

  return normalize(tangentX * H.x + tangentY * H.y + normal * H.z);
}

float DistributionGGX(float dotNH, float roughness) {
  const float alpha = roughness * roughness;
	const float alpha2 = alpha * alpha;
	const float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
	return (alpha2) / (PI * denom * denom);
}

vec3 PrefilterEnvMap(vec3 R, float roughness) {
	const uint sampleCount = 32u;
  const vec3 N = R;
	const vec3 V = R;
	const float envMapDim = float(textureSize(envMap, 0).s);

	vec3 color = vec3(0.0);
	float totalWeight = 0.0;
	for (uint i = 0u; i < sampleCount; ++i) {
		const vec2 Xi = Hammersley2D(i, sampleCount);
		const vec3 H = ImportanceSampleGGX(Xi, roughness, N);
		const vec3 L = 2.0 * dot(V, H) * H - V;
		const float dotNL = clamp(dot(N, L), 0.0, 1.0);
		if (dotNL > 0.0) {
			 const float dotNH = clamp(dot(N, H), 0.0, 1.0);
			 const float dotVH = clamp(dot(V, H), 0.0, 1.0);

			 const float pdf = DistributionGGX(dotNH, roughness) * dotNH / (4.0 * dotVH) + 0.0001;
			 const float omegaS = 1.0 / (float(sampleCount) * pdf);
			 const float omegaP = 4.0 * PI / (6.0 * envMapDim * envMapDim);
			 const float mipLevel = roughness == 0.0 ? 0.0 : max(0.5 * log2(omegaS / omegaP) + 1.0, 0.0);

			 color += textureLod(envMap, L, mipLevel).rgb * dotNL;
			 totalWeight += dotNL;
		}
	}

	return color / totalWeight;
}

void main() {
  const vec3 N = normalize(inLocalPos);
	outColor = vec4(PrefilterEnvMap(N, PC.Roughness), 1.0);
}
