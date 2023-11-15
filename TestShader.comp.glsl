#version 460 core

layout(set = 0, binding = 0) buffer InBuffer {
  float In;
};

layout(set = 0, binding = 1) buffer OutBuffer {
  float Out;
};

layout(push_constant) uniform PushConstant {
  float Data;
};

void main() {
  Out = In * Data;
}
