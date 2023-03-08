#version 460 core

const float Pi = 3.14159265359;

layout(location = 0) in vec3 inLocalPos;

layout(set = 0, binding = 0) uniform samplerCube TexEnvironment;

layout(push_constant) uniform PushConstant {
	layout(offset = 64) float Roughness;
} PC;

layout(location = 0) out vec4 outColor;

float Random(vec2 co) {
	const float a = 12.9898;
	const float b = 78.233;
	const float c = 43758.5453;
	float dt = dot(co.xy, vec2(a, b));
	float sn = mod(dt, 3.14);

	return fract(sin(sn) * c);
}

float DistributionGGX(float NdotH, float roughness) {
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;

	return alpha2 / (Pi * denom * denom);
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

vec3 Prefilter(vec3 N, float roughness) {
	const uint sampleCount = 32u;
	float envMapDim = float(textureSize(TexEnvironment, 0).s);
	N *= vec3(1, -1, 1);

	vec3 color = vec3(0.0);
	float totalWeight = 0.0;
	for (uint i = 0u; i < sampleCount; ++i) {
		vec2 Xi = Hammersley2D(i, sampleCount);
		vec3 H = ImportanceSampleGGX(Xi, roughness, N);
		vec3 L = 2.0 * dot(N, H) * H - N;
		float NdotL = clamp(dot(N, L), 0.0, 1.0);
		if (NdotL > 0.0) {
			float NdotH = clamp(dot(N, H), 0.0, 1.0);

			float pdf = DistributionGGX(NdotH, roughness) * NdotH / (4.0 * NdotH) + 0.0001;
			float omegaS = 1.0 / (float(sampleCount) * pdf);
			float omegaP = 4.0 * Pi / (6.0 * envMapDim * envMapDim);
			float mipLevel = roughness == 0.0 ? 0.0 : max(0.5 * log2(omegaS / omegaP) + 1.0, 0.0);

			color += textureLod(TexEnvironment, L, mipLevel).rgb * NdotL;
			totalWeight += NdotL;
		}
	}

	return color / totalWeight;
}

void main() {
	vec3 N = normalize(inLocalPos);
	outColor = vec4(Prefilter(N, PC.Roughness), 1.0);
}
