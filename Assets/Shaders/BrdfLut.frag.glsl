#version 450 core

layout(constant_id = 0) const uint SAMPLE_COUNT = 1024u;

layout(location = 0) in vec2 inUV;

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

float GSchlickSmithGGX(float dotNL, float dotNV, float roughness) {
  const float k = (roughness * roughness) / 2.0;
	const float GL = dotNL / (dotNL * (1.0 - k) + k);
	const float GV = dotNV / (dotNV * (1.0 - k) + k);
	return GL * GV;
}

vec2 BRDF(float NoV, float roughness) {
  const vec3 N = vec3(0.0, 0.0, 1.0);
	const vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

  vec2 LUT = vec2(0.0);
	for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
    const vec2 Xi = Hammersley2D(i, SAMPLE_COUNT);
		const vec3 H = ImportanceSampleGGX(Xi, roughness, N);
		const vec3 L = 2.0 * dot(V, H) * H - V;

		const float dotNL = max(dot(N, L), 0.0);
		const float dotNV = max(dot(N, V), 0.0);
		const float dotVH = max(dot(V, H), 0.0);
		const float dotNH = max(dot(N, H), 0.0);

    if (dotNL > 0.0) {
      const float G = GSchlickSmithGGX(dotNL, dotNV, roughness);
			const float GVis = (G * dotVH) / (dotNH * dotNV);
			const float Fc = pow(1.0 - dotVH, 5.0);
			LUT += vec2((1.0 - Fc) * GVis, Fc * GVis);
    }
	}
	return LUT / float(SAMPLE_COUNT);
}

void main() {
  outColor = vec4(BRDF(inUV.s, 1.0 - inUV.t), 0.0, 1.0);
}
