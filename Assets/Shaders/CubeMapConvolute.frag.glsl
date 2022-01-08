#version 450 core

layout(location = 0) in vec3 inLocalPos;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

void main() {
	const vec3 N = normalize(inLocalPos);
	vec3 irradiance = vec3(0.0);

	vec3 up = vec3(0, 1, 0);
	const vec3 right = normalize(cross(up, N));
	up = normalize(cross(N, right));

	const float sampleDelta = 0.025;
	float sampleCount = 0.0;
	for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
		for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
			const vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			const vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
			irradiance += texture(envMap, sampleVec).rgb * cos(theta) * sin(theta);
			sampleCount++;
		}
	}
	irradiance = PI * irradiance * (1.0 / sampleCount);

	outColor = vec4(irradiance, 1.0);
}
