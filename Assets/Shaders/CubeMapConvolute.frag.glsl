#version 450 core

layout(location = 0) in vec3 inLocalPos;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const float TWO_PI = PI * 2.0;
const float HALF_PI = PI * 0.5;
const float deltaPhi = TWO_PI / 180.0f;
const float deltaTheta = HALF_PI / 64.0f;

void main() {
	const vec3 N = normalize(inLocalPos);
	vec3 up = vec3(0, 1, 0);
	const vec3 right = normalize(cross(up, N));
	up = normalize(cross(N, right));

	uint sampleCount = 0u;
	vec3 irradiance = vec3(0.0);
	for (float phi = 0.0; phi < TWO_PI; phi += deltaPhi) {
		for (float theta = 0.0; theta < HALF_PI; theta += deltaTheta) {
			const vec3 temp = cos(phi) * right + sin(phi) * up;
			const vec3 sampleVec = cos(theta) * N + sin(theta) * temp;
			irradiance += texture(envMap, sampleVec).rgb * cos(theta) * sin(theta);
			sampleCount++;
		}
	}

	outColor = vec4(PI * irradiance / float(sampleCount), 1.0);
}
