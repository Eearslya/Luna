#version 460 core

const float Pi = 3.14159265359;
const float TwoPi = Pi * 2.0;
const float HalfPi = Pi * 0.5;
const float DeltaPhi = TwoPi / 180.0;
const float DeltaTheta = HalfPi / 64.0;

layout(location = 0) in vec3 inLocalPos;

layout(set = 0, binding = 0) uniform samplerCube TexEnvironment;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 N = normalize(inLocalPos);
	vec3 up = vec3(0, 1, 0);
	vec3 right = normalize(cross(up, N));
	up = normalize(cross(N, right));

	uint sampleCount = 0u;
	vec3 irradiance = vec3(0.0);
	for (float phi = 0.0; phi < TwoPi; phi += DeltaPhi) {
		for (float theta = 0.0; theta < HalfPi; theta += DeltaTheta) {
			vec3 temp = cos(phi) * right + sin(phi) * up;
			vec3 sampleVec = cos(theta) * N + sin(theta) * temp;
			irradiance += texture(TexEnvironment, sampleVec).rgb * cos(theta) * sin(theta);
			sampleCount++;
		}
	}

	outColor = vec4(Pi * irradiance / float(sampleCount), 1.0);
}
