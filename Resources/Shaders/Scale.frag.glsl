#version 460 core

layout(location = 0) in vec2 inUV;

#if ATTACHMENT_0
layout(set = 0, binding = 0) uniform sampler2D Input0;
layout(location = 0) out vec4 Output0;
#endif

#if ATTACHMENT_1
layout(set = 0, binding = 1) uniform sampler2D Input1;
layout(location = 1) out vec4 Output1;
#endif

#if ATTACHMENT_2
layout(set = 0, binding = 2) uniform sampler2D Input2;
layout(location = 2) out vec4 Output2;
#endif

#if ATTACHMENT_3
layout(set = 0, binding = 3) uniform sampler2D Input3;
layout(location = 3) out vec4 Output3;
#endif

void main() {
#if ATTACHMENT_0
	Output0 = textureLod(Input0, inUV, 0);
#endif
#if ATTACHMENT_1
	Output1 = textureLod(Input1, inUV, 0);
#endif
#if ATTACHMENT_2
	Output2 = textureLod(Input2, inUV, 0);
#endif
#if ATTACHMENT_3
	Output3 = textureLod(Input3, inUV, 0);
#endif
}
