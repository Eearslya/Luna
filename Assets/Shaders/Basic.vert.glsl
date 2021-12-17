#version 450 core

const vec3 positions[3] = vec3[3](
	vec3(1.f,1.f, 0.0f),
	vec3(-1.f,1.f, 0.0f),
	vec3(0.f,-1.f, 0.0f)
);

const vec3 colors[3] = vec3[3](
	vec3(0, 1, 0),
	vec3(0, 0, 1),
	vec3(1, 0, 0)
);

layout(location = 0) out vec3 outColor;

void main() {
	outColor = colors[gl_VertexIndex];
	gl_Position = vec4(positions[gl_VertexIndex], 1.0f);
}
