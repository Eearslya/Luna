#version 450 core

layout(location = 0) in vec3 inLocalPos;

layout(push_constant) uniform SkyboxData {
	float DebugView;
} PC;

layout(set = 1, binding = 0) uniform samplerCube TexSkybox;
layout(set = 1, binding = 1) uniform samplerCube TexIrradiance;
layout(set = 1, binding = 2) uniform samplerCube TexPrefiltered;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 envColor = texture(TexSkybox, inLocalPos).rgb;

	if (PC.DebugView > 0.0) {
		const int index = int(PC.DebugView);
		switch(index) {
			case 1:
				envColor.rgb = texture(TexIrradiance, inLocalPos).rgb;
				break;
			case 2:
				envColor.rgb = texture(TexPrefiltered, inLocalPos).rgb;
				break;
		}
	}

	envColor = envColor / (envColor + vec3(1.0));
	outColor = vec4(envColor, 1.0);
}
