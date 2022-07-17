#version 450 core

const vec3 positions[6] = vec3[6](
	vec3(-0.5f,  0.5f, 0.0f),
	vec3(-0.5f, -0.5f, 0.0f),
	vec3( 0.5f, -0.5f, 0.0f),
	vec3( 0.5f, -0.5f, 0.0f),
	vec3( 0.5f,  0.5f, 0.0f),
	vec3(-0.5f,  0.5f, 0.0f)
);

layout(set = 0, binding = 0) uniform SceneData {
	mat4 Projection;
	mat4 View;
} Scene;

layout(push_constant) uniform PushConstant {
	mat4 Model;
} PC;

void main(){
	gl_Position = Scene.Projection * Scene.View * PC.Model * vec4(positions[gl_VertexIndex], 1.0f);
}
