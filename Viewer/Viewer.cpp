#include <Luna/Luna.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

constexpr const char* vertex = R"VERTEX(
#version 460 core

const vec3 positions[3] = vec3[3](
  vec3(0.0f, -1.0f, 0.0f),
  vec3(-1.0f, 1.0f, 0.0f),
  vec3(1.0f, 1.0f, 0.0f)
);
const vec3 colors[3] = vec3[3](
  vec3(1.0f, 0.0f, 0.0f),
  vec3(0.0f, 0.0f, 1.0f),
  vec3(0.0f, 1.0f, 0.0f)
);

layout(location = 0) out vec3 outColor;

void main() {
  outColor = colors[gl_VertexIndex];
  gl_Position = vec4(positions[gl_VertexIndex], 1.0f);
}
)VERTEX";

constexpr const char* fragment = R"FRAGMENT(
#version 460 core

layout(location = 0) in vec3 inColor;

layout(location = 0) out vec4 outColor;

void main() {
  outColor = vec4(inColor, 1.0f);
}
)FRAGMENT";

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto& device = GetDevice();

		_program = device.RequestProgram(vertex, fragment);
	}

	virtual void OnUpdate() override {
		auto& device = GetDevice();

		auto cmd                   = device.RequestCommandBuffer();
		auto rpInfo                = device.GetSwapchainRenderPass(Luna::Vulkan::SwapchainRenderPassType::Depth);
		rpInfo.ColorClearValues[0] = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f);
		cmd->BeginRenderPass(rpInfo);
		cmd->SetProgram(_program);
		cmd->Draw(3);
		cmd->EndRenderPass();
		device.Submit(cmd);
	}

 private:
	Luna::Vulkan::Program* _program = nullptr;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
