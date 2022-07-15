#include <GLFW/glfw3.h>

#include <iostream>

#include "Utility/Log.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandBuffer.hpp"
#include "Vulkan/Context.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Shader.hpp"

using namespace Luna;

static constexpr const char* VertexShader = R"GLSL(
#version 460 core

void main() {}
)GLSL";

static constexpr const char* FragmentShader = R"GLSL(
#version 460 core

layout(location = 0) out vec4 outColor;

void main() {
  outColor = vec4(1, 1, 1, 1);
}
)GLSL";

static constexpr const char* ComputeShader = R"GLSL(
#version 460 core

layout(local_size_x = 64) in;

layout(std430, set = 0, binding = 0) readonly buffer BufferA {
  float InputA[];
};
layout(std430, set = 0, binding = 1) readonly buffer BufferB {
  float InputB[];
};
layout(std430, set = 1, binding = 0) writeonly buffer BufferC {
  float Output[];
};

void main() {
  uint ident = gl_GlobalInvocationID.x;
  Output[ident] = InputA[ident] * InputB[ident];
}
)GLSL";

int main(int argc, const char** argv) {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	try {
		Vulkan::Context context;
		Vulkan::Device device(context);

		{
			const uint8_t bufferData[64] = {0};
			const Vulkan::BufferCreateInfo bufferCI(
				Vulkan::BufferDomain::Device, 64, vk::BufferUsageFlagBits::eStorageBuffer);
			auto buffer = device.CreateBuffer(bufferCI, bufferData);

			auto cmd = device.RequestCommandBuffer();
			device.Submit(cmd);
		}
		device.NextFrame();

		{
			const uint32_t imageData[] = {0u, ~0u, 0u, ~0u, ~0u, 0u, ~0u, 0u, 0u, ~0u, 0u, ~0u, ~0u, 0u, ~0u, 0u};
			const Vulkan::ImageCreateInfo imageCI =
				Vulkan::ImageCreateInfo::Immutable2D(4, 4, vk::Format::eR8G8B8A8Unorm, true);
			const Vulkan::ImageInitialData initialImage = {.Data = imageData};
			auto image                                  = device.CreateImage(imageCI, &initialImage);
			auto& view                                  = image->GetView();

			auto cmd = device.RequestCommandBuffer();
			device.Submit(cmd);
		}
		device.NextFrame();

		{ auto* program = device.RequestProgram(VertexShader, FragmentShader); }
		device.NextFrame();

		{
			auto* program = device.RequestProgram(ComputeShader);

			float initialA[64];
			float initialB[64];
			for (int i = 0; i < 64; ++i) {
				initialA[i] = float(i) * 2.0f;
				initialB[i] = float(i) * 4.0f;
			}

			const Vulkan::BufferCreateInfo bufferCI(
				Vulkan::BufferDomain::Device, sizeof(initialA), vk::BufferUsageFlagBits::eStorageBuffer);
			auto inputA = device.CreateBuffer(bufferCI, initialA);
			auto inputB = device.CreateBuffer(bufferCI, initialB);
			auto output = device.CreateBuffer(bufferCI);

			auto cmd = device.RequestCommandBuffer();
			cmd->SetProgram(program);
			cmd->SetStorageBuffer(0, 0, *inputA);
			cmd->SetStorageBuffer(0, 1, *inputB);
			cmd->SetStorageBuffer(1, 0, *output);
			cmd->Dispatch(1, 1, 1);
			device.Submit(cmd);
		}
		device.NextFrame();

		device.WaitIdle();
	} catch (const std::exception& e) {
		std::cerr << "Fatal uncaught exception when initializing Vulkan:\n\t" << e.what() << std::endl;
		return 1;
	}

	Log::Shutdown();

	return 0;
}
