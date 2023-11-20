#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderCompiler.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Renderer/UIManager.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

namespace Luna {
static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
	Hash LastSwapchainHash = 0;

	RenderGraph Graph;

	Vulkan::BufferHandle BufferIn;
	Vulkan::BufferHandle BufferOut;
	Vulkan::Program* compShader = nullptr;
} State;

bool Renderer::Initialize() {
	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);
	State.Device  = MakeHandle<Vulkan::Device>(*State.Context);

	ShaderCompiler shaderCompiler;
	shaderCompiler.SetSourceFromFile("file://TestShader.comp.glsl", Vulkan::ShaderStage::Compute);
	shaderCompiler.Preprocess();
	std::string shaderError;
	auto shaderCode = shaderCompiler.Compile(shaderError);
	if (!shaderError.empty()) { Log::Error("Renderer", "Test Shader Fail: {}", shaderError); }
	State.compShader = State.Device->RequestProgram(shaderCode);

	const float initialNumber = 23.0f;
	const Vulkan::BufferCreateInfo bufferCI(
		Vulkan::BufferDomain::Host, sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer);
	State.BufferIn  = State.Device->CreateBuffer(bufferCI, &initialNumber);
	State.BufferOut = State.Device->CreateBuffer(bufferCI);

	return true;
}

void Renderer::Shutdown() {
	State.BufferIn.Reset();
	State.BufferOut.Reset();
	State.Graph.Reset();
	State.Device.Reset();
	State.Context.Reset();
}

Vulkan::Device& Renderer::GetDevice() {
	return *State.Device;
}

static void BakeRenderGraph() {
	// Preserve the RenderGraph buffer objects on the chance they don't need to be recreated.
	auto buffers = State.Graph.ConsumePhysicalBuffers();

	// Reset the RenderGraph state and proceed forward a frame to clean up the graph resources.
	State.Graph.Reset();
	State.Device->NextFrame();

	const auto swapchainExtent = Engine::GetMainWindow()->GetSwapchain().GetExtent();
	const auto swapchainFormat = Engine::GetMainWindow()->GetSwapchain().GetFormat();
	const ResourceDimensions backbufferDimensions{
		.Format = swapchainFormat, .Width = swapchainExtent.width, .Height = swapchainExtent.height};
	State.Graph.SetBackbufferDimensions(backbufferDimensions);

	/*
	auto& pass = State.Graph.AddPass("Main");
	AttachmentInfo main;
	pass.AddColorOutput("Final", main);
	pass.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
	  if (value) { *value = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f); }
	  return true;
	});
	*/

	{
		Luna::AttachmentInfo uiColor;

		auto& ui = State.Graph.AddPass("UI");

		ui.AddColorOutput("UI", uiColor);

		ui.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
			if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
			return true;
		});
		ui.SetBuildRenderPass([](Vulkan::CommandBuffer& cmd) { UIManager::Render(cmd); });
	}

	State.Graph.SetBackbufferSource("UI");
	State.Graph.Bake(*State.Device);
	State.Graph.InstallPhysicalBuffers(buffers);

	State.Graph.Log();
}

void Renderer::Render() {
	auto& device = *State.Device;
	device.NextFrame();

	if (!Engine::GetMainWindow()) { return; }

	const bool acquired = Engine::GetMainWindow()->GetSwapchain().Acquire();
	if (!acquired) { return; }

	if (Engine::GetMainWindow()->GetSwapchainHash() != State.LastSwapchainHash) {
		BakeRenderGraph();
		State.LastSwapchainHash = Engine::GetMainWindow()->GetSwapchainHash();
	}

	TaskComposer composer;
	State.Graph.SetupAttachments(*State.Device, &State.Device->GetSwapchainView());
	State.Graph.EnqueueRenderPasses(*State.Device, composer);
	composer.GetOutgoingTask()->Wait();

	/*
	auto cmd = device.RequestCommandBuffer();

	auto rpInfo           = device.GetSwapchainRenderPass();
	rpInfo.ClearColors[0] = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f);
	cmd->BeginRenderPass(rpInfo);
	cmd->EndRenderPass();

	cmd->SetProgram(State.compShader);
	cmd->SetStorageBuffer(0, 0, *State.BufferIn);
	cmd->SetStorageBuffer(0, 1, *State.BufferOut);
	const float multiplier = 2.0f;
	cmd->PushConstants(multiplier);
	cmd->Dispatch(1, 1, 1);

	device.Submit(cmd);
	*/

	Engine::GetMainWindow()->GetSwapchain().Present();
}
}  // namespace Luna
