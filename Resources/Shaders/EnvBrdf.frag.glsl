#version 460 core

const float Pi = 3.14159265359;

layout(constant_id = 0) const uint SampleCount = 1024u;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

float Random(vec2 co) {
	const float a = 12.9898;
	const float b = 78.233;
	const float c = 43758.5453;
	float dt = dot(co.xy, vec2(a, b));
	float sn = mod(dt, 3.14);

	return fract(sin(sn) * c);
}

vec2 Hammersley2D(uint i, uint N) {
	uint bits = (i << 16u) | (i >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
	bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
	bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
	float rdi = float(bits) * 2.3283064365386963e-10;

	return vec2(float(i) / float(N), rdi);
}

vec3 ImportanceSampleGGX(vec2 Xi, float roughness, vec3 N) {
	float alpha = roughness * roughness;
	float phi = 2.0 * Pi * Xi.x + Random(N.xz) * 0.1;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

	vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
	vec3 tangentX = normalize(cross(up, N));
	vec3 tangentY = normalize(cross(N, tangentX));

	return normalize(tangentX * H.x + tangentY * H.y + N * H.z);
}

float GSchlickSmithGGX(float NdotL, float NdotV, float roughness) {
	float k = (roughness * roughness) / 2.0;
	float GL = NdotL / (NdotL * (1.0 - k) + k);
	float GV = NdotV / (NdotV * (1.0 - k) + k);

	return GL * GV;
}

vec2 BRDF(float NoV, float roughness) {
	vec3 N = vec3(0, 0, 1);
	vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

	vec2 LUT = vec2(0.0);
	for (uint i = 0u; i < SampleCount; ++i) {
		vec2 Xi = Hammersley2D(i, SampleCount);
		vec3 H = ImportanceSampleGGX(Xi, roughness, N);
		vec3 L = 2.0 * dot(V, H) * H - V;

		float NdotH = max(dot(N, H), 0.0);
		float NdotL = max(dot(N, L), 0.0);
		float NdotV = max(dot(N, V), 0.0);
		float VdotH = max(dot(V, H), 0.0);

		if (NdotL > 0.0) {
			float G = GSchlickSmithGGX(NdotL, NdotV, roughness);
			float GVis = (G * VdotH) / (NdotH * NdotV);
			float Fc = pow(1.0 - VdotH, 5.0);
			LUT += vec2((1.0 - Fc) * GVis, Fc * GVis);
		}
	}

	return LUT / float(SampleCount);
}

void main() {
	outColor = vec4(BRDF(inUV.s, 1.0 - inUV.t), 0.0, 0.0);
}
