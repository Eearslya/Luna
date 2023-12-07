#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstant {
  mat4 Camera;
  mat4 Model;
} PC;

layout(location = 0) out vec3 outNormal;

void main() {
  outNormal = inNormal;
  gl_Position = PC.Camera * PC.Model * vec4(inPosition, 1.0);
}
