#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexcoord0;
layout(location = 4) in vec2 inTexcoord1;
layout(location = 5) in vec4 inColor0;
layout(location = 6) in uvec4 inJoints0;
layout(location = 7) in vec4 inWeights0;

layout(set = 0, binding = 0) uniform SceneUBO {
  mat4 Projection;
  mat4 View;
	mat4 ViewProjection;
	vec4 ViewPosition;
	vec4 LightPosition;
} Scene;

layout(push_constant) uniform PushConstantData {
  mat4 Node;
} PC;

struct VertexOut {
	vec3 WorldPos;
	vec2 UV0;
	vec2 UV1;
	vec4 Color0;
	mat3 NormalMat;
};

layout(location = 0) out VertexOut Out;

void main() {
	mat4 model = PC.Node;
	vec4 locPos = model * vec4(inPosition, 1.0f);

	mat4 normalMatrix = mat4(inverse(transpose(model)));
	vec3 T = normalize((normalMatrix * vec4(inTangent.xyz, 0.0)).xyz);
	vec3 B = normalize((normalMatrix * vec4(cross(inNormal.xyz, inTangent.xyz) * inTangent.w, 0.0)).xyz);
	vec3 N = normalize((normalMatrix * vec4(inNormal.xyz, 0.0)).xyz);

	Out.WorldPos = locPos.xyz / locPos.w;
	Out.UV0 = inTexcoord0;
	Out.UV1 = inTexcoord1;
	Out.Color0 = inColor0;
	Out.NormalMat = mat3(T, B, N);

	gl_Position = Scene.ViewProjection * vec4(Out.WorldPos, 1.0f);
}
