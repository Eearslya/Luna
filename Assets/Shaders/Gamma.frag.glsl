#version 450 core

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput InputHDR;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = subpassLoad(InputHDR);
}
