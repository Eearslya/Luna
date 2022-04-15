#version 450 core

layout (location = 0) out vec2 outUV0;

void main() {
	outUV0 = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(outUV0 * 2.0f - 1.0f, 0.0f, 1.0f);
}
