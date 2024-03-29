#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexcoord;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PushConstant {
	vec2 Scale;
	vec2 Translate;
	uint SampleMode;
};

layout(location = 0) out vec2 outTexcoord;
layout(location = 1) out vec4 outColor;

void main() {
	outTexcoord = inTexcoord;
	outColor = inColor;

  vec2 position = inPosition * Scale + Translate;
	gl_Position = vec4(vec2(position.x, -1.0 * position.y), 0, 1);
}
