#version 460 core

layout(location = 0) in vec2 inTexcoord;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform sampler2D Texture;

layout(push_constant) uniform PushConstant {
	vec2 Scale;
	vec2 Translate;
	uint SampleMode;
};

layout(location = 0) out vec4 outColor;

void main() {
	vec4 texSample = texture(Texture, inTexcoord);

	switch(SampleMode) {
		case 1: // ImGui Font
			texSample = vec4(1, 1, 1, texSample.r);
			break;
		case 2: // Grayscale
			texSample = texSample.rrra;
			break;
		default: // Standard
		  break;
	}

	outColor = inColor * texSample;
}
