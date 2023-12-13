#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/Camera.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Scene.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Renderer/UIManager.hpp>
#include <Luna/Utility/Time.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
struct PerFrameBuffer {
	Vulkan::Buffer& Get(vk::DeviceSize size = vk::WholeSize) {
		const auto frameIndex = Renderer::GetDevice().GetFrameIndex();
		if (frameIndex >= Buffers.size()) { Buffers.resize(frameIndex + 1); }

		if (!Buffers[frameIndex] || (size != vk::WholeSize && Buffers[frameIndex]->GetCreateInfo().Size < size)) {
			const Vulkan::BufferCreateInfo bufferCI(
				Vulkan::BufferDomain::Host,
				size,
				vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
			Buffers[frameIndex] = Renderer::GetDevice().CreateBuffer(bufferCI);
		}

		return *Buffers[frameIndex];
	}

	void Reset() {
		Buffers.clear();
	}

	std::vector<Vulkan::BufferHandle> Buffers;
};

struct SceneData {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 ViewProjection;
	glm::vec4 CameraPosition;
	glm::vec4 FrustumPlanes[6];
};

struct ComputeUniforms {
	uint32_t MeshletCount = 0;
};

struct DebugLine {
	glm::vec3 Start;
	glm::vec3 End;
	glm::vec3 Color;
};

static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
	Hash LastSwapchainHash = 0;

	RenderGraph Graph;
	ElapsedTime FrameTimer;

	EditorCamera Camera;
	bool CameraActive            = false;
	glm::dvec2 LastMousePosition = glm::dvec2(0);
	Scene Scene;
	RenderScene RenderScene;
	ShaderProgramVariant* Program;

	PerFrameBuffer SceneBuffer;
	PerFrameBuffer ComputeUniforms;
	PerFrameBuffer MeshletBuffer;
	PerFrameBuffer TransformBuffer;
	PerFrameBuffer DebugLinesBuffer;

	std::vector<DebugLine> DebugLines;
	SceneData SceneData;
	glm::mat4 CullFrustum = glm::mat4(1.0f);

	bool FreezeCullFrustum = false;
	bool ShowCullFrustum   = false;
} State;

static glm::vec2 WorldToPixel(const glm::mat4& viewProjection, const glm::vec3& worldPos, const glm::vec2& imageSize) {
	glm::vec4 ndc = viewProjection * glm::vec4(worldPos, 1.0f);
	ndc /= ndc.w;
	glm::vec2 remap01 = glm::clamp((ndc + 1.0f) * 0.5f, 0.0f, 1.0f);
	remap01.y         = 1.0f - remap01.y;

	return glm::clamp(remap01 * imageSize, glm::vec2(0), imageSize);
}

static glm::vec3 UnprojectUV(const glm::mat4& invVP, float depth, glm::vec2 uv) {
	const glm::vec4 ndc   = glm::vec4(uv * 2.0f - 1.0f, depth, 1.0f);
	const glm::vec4 world = invVP * ndc;
	return glm::vec3(world / world.w);
}

static void DrawLine(const glm::vec3& start,
                     const glm::vec3& end,
                     ImColor color = ImColor(255, 255, 255, 255),
                     float width   = 1.0f) {
	const auto& backbuffer     = State.Graph.GetTextureResource("Final");
	const auto& backbufferDims = State.Graph.GetResourceDimensions(backbuffer);

	const glm::vec2 offset(0, 0);
	const glm::vec2 imageSize(backbufferDims.Width, backbufferDims.Height);

	const auto startPixel = WorldToPixel(State.SceneData.ViewProjection, start, imageSize) + offset;
	const auto endPixel   = WorldToPixel(State.SceneData.ViewProjection, end, imageSize) + offset;

	auto* drawList = ImGui::GetBackgroundDrawList();
	drawList->AddLine(ImVec2(startPixel.x, startPixel.y), ImVec2(endPixel.x, endPixel.y), color, width);
}

static void DrawDebugLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color = glm::vec3(1)) {
	State.DebugLines.emplace_back(start, end, color);
}

static void RendererUI() {
	if (ImGui::Begin("Model")) {
		ImGui::Checkbox("Freeze Culling Frustum", &State.FreezeCullFrustum);
		ImGui::Checkbox("Show Culling Frustum", &State.ShowCullFrustum);
	}
	ImGui::End();

	if (State.ShowCullFrustum) {
		const auto& invVP    = glm::inverse(State.CullFrustum);
		glm::vec3 corners[8] = {glm::vec3(-1, -1, 0),
		                        glm::vec3(1, -1, 0),
		                        glm::vec3(-1, 1, 0),
		                        glm::vec3(1, 1, 0),
		                        glm::vec3(-1, -1, 1),
		                        glm::vec3(1, -1, 1),
		                        glm::vec3(-1, 1, 1),
		                        glm::vec3(1, 1, 1)};
		for (auto& corner : corners) {
			glm::vec4 c = invVP * glm::vec4(corner, 1.0f);
			corner      = c / c.w;
		}

		DrawDebugLine(corners[0], corners[1]);
		DrawDebugLine(corners[0], corners[2]);
		DrawDebugLine(corners[3], corners[1]);
		DrawDebugLine(corners[3], corners[2]);

		DrawDebugLine(corners[4], corners[5]);
		DrawDebugLine(corners[4], corners[6]);
		DrawDebugLine(corners[7], corners[5]);
		DrawDebugLine(corners[7], corners[6]);

		DrawDebugLine(corners[0], corners[4]);
		DrawDebugLine(corners[1], corners[5]);
		DrawDebugLine(corners[2], corners[6]);
		DrawDebugLine(corners[3], corners[7]);
	}
}

bool Renderer::Initialize() {
	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);
	State.Device  = MakeHandle<Vulkan::Device>(*State.Context);

	State.Camera.SetPosition({0, 0, 0.025});
	State.Scene.LoadModel("res://Models/Bistro.glb");

	Input::OnKey += [](Key key, InputAction action, InputMods mods) {};
	Input::OnMouseButton += [](MouseButton button, InputAction action, InputMods mods) {
		auto& io = ImGui::GetIO();
		if (button == MouseButton::Right) {
			if (!State.CameraActive && !io.WantCaptureMouse) {
				State.CameraActive = true;
				Input::SetCursorHidden(true);
			} else if (State.CameraActive) {
				State.CameraActive = false;
				Input::SetCursorHidden(false);
			}
		}
	};
	Input::OnMouseMoved += [](const glm::dvec2& pos) {
		if (State.CameraActive) {
			const float sensitivity = 0.5f;
			State.Camera.Rotate(pos.y * sensitivity, -pos.x * sensitivity);
		}
		State.LastMousePosition = pos;
	};

	return true;
}

void Renderer::Shutdown() {
	State.Scene.Clear();
	State.SceneBuffer.Reset();
	State.DebugLinesBuffer.Reset();
	State.ComputeUniforms.Reset();
	State.MeshletBuffer.Reset();
	State.TransformBuffer.Reset();
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

	State.Program =
		ShaderManager::RegisterGraphics("res://Shaders/StaticMesh.vert.glsl", "res://Shaders/StaticMesh.frag.glsl")
			->RegisterVariant();

	constexpr static unsigned int MaxMeshlets          = 262'144;
	constexpr static unsigned int MaxMeshletsPerBatch  = 65'536;
	constexpr static unsigned int MaxMeshletBatches    = MaxMeshlets / MaxMeshletsPerBatch;
	constexpr static unsigned int MaxTrianglesPerBatch = MaxMeshletsPerBatch * 64;
	constexpr static unsigned int MaxIndicesPerBatch   = MaxTrianglesPerBatch * 3;

	{
		auto& pass = State.Graph.AddPass("Meshlet Cull", RenderGraphQueueFlagBits::Compute);

		BufferInfo visibleMeshletsInfo = {.Size  = sizeof(uint32_t) * MaxMeshlets,
		                                  .Usage = vk::BufferUsageFlagBits::eStorageBuffer};
		auto& visibleMeshletsRes       = pass.AddStorageOutput("VisibleMeshletIDs", visibleMeshletsInfo);

		BufferInfo cullTriangleDispatchInfo = {.Size  = sizeof(vk::DispatchIndirectCommand) * MaxMeshletBatches,
		                                       .Usage = vk::BufferUsageFlagBits::eStorageBuffer |
		                                                vk::BufferUsageFlagBits::eIndirectBuffer |
		                                                vk::BufferUsageFlagBits::eTransferDst};
		auto& cullTriangleDispatchRes       = pass.AddStorageOutput("CullTriangleDispatch", cullTriangleDispatchInfo);

		pass.SetBuildRenderPass([&](Vulkan::CommandBuffer& cmd) {
			auto& visibleMeshlets      = State.Graph.GetPhysicalBufferResource(visibleMeshletsRes);
			auto& cullTriangleDispatch = State.Graph.GetPhysicalBufferResource(cullTriangleDispatchRes);

			std::array<vk::DispatchIndirectCommand, MaxMeshletBatches> dispatch;
			dispatch.fill(vk::DispatchIndirectCommand(0, 1, 1));
			cmd.UpdateBuffer(cullTriangleDispatch, dispatch.size() * sizeof(dispatch[0]), dispatch.data());
			cmd.BufferBarrier(cullTriangleDispatch,
			                  vk::PipelineStageFlagBits2::eTransfer,
			                  vk::AccessFlagBits2::eTransferWrite,
			                  vk::PipelineStageFlagBits2::eComputeShader,
			                  vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite);

			struct PushConstant {
				uint32_t BatchID;
				uint32_t MeshletsPerBatch;
			};
			PushConstant pc = PushConstant{.BatchID = 0, .MeshletsPerBatch = MaxMeshletsPerBatch};

			cmd.SetProgram(
				ShaderManager::RegisterCompute("res://Shaders/CullMeshlets.comp.glsl")->RegisterVariant()->GetProgram());
			cmd.SetUniformBuffer(0, 0, State.SceneBuffer.Get());
			cmd.SetUniformBuffer(1, 0, State.ComputeUniforms.Get());
			cmd.SetStorageBuffer(1, 1, cullTriangleDispatch);
			cmd.SetStorageBuffer(1, 2, visibleMeshlets);
			cmd.SetStorageBuffer(1, 3, State.MeshletBuffer.Get());
			cmd.SetStorageBuffer(1, 4, State.TransformBuffer.Get());

			uint32_t meshletCount = std::min<uint32_t>(State.RenderScene.Meshlets.size(), MaxMeshlets);
			while (meshletCount > 0) {
				if (pc.BatchID >= MaxMeshletBatches) { break; }

				const auto dispatch = std::min(meshletCount, MaxMeshletsPerBatch);
				cmd.PushConstants(pc);
				cmd.Dispatch(dispatch, 1, 1);
				pc.BatchID++;

				const auto b = vk::MemoryBarrier2(vk::PipelineStageFlagBits2::eAllCommands,
				                                  vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
				                                  vk::PipelineStageFlagBits2::eAllCommands,
				                                  vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
				const vk::DependencyInfo superBarrier({}, b);
				cmd.Barrier(superBarrier);

				if (meshletCount > MaxMeshletsPerBatch) {
					meshletCount -= MaxMeshletsPerBatch;
				} else {
					meshletCount = 0;
				}
			}
		});
	}

	{
		auto& pass = State.Graph.AddPass("Triangle Cull", RenderGraphQueueFlagBits::Compute);

		auto& visibleMeshletsRes      = pass.AddStorageInput("VisibleMeshletIDs");
		auto& cullTriangleDispatchRes = pass.AddIndirectInput("CullTriangleDispatch");

		BufferInfo drawIndirectInfo = {.Size  = sizeof(vk::DrawIndexedIndirectCommand) * MaxMeshletBatches,
		                               .Usage = vk::BufferUsageFlagBits::eStorageBuffer |
		                                        vk::BufferUsageFlagBits::eIndirectBuffer |
		                                        vk::BufferUsageFlagBits::eTransferDst};
		auto& drawIndirectRes       = pass.AddStorageOutput("DrawIndirect", drawIndirectInfo);

		BufferInfo meshletIndicesInfo = {.Size  = MaxIndicesPerBatch * MaxMeshletBatches * sizeof(uint32_t),
		                                 .Usage = vk::BufferUsageFlagBits::eIndexBuffer};
		auto& meshletIndicesRes       = pass.AddStorageOutput("MeshletIndices", meshletIndicesInfo);

		pass.SetBuildRenderPass([&](Vulkan::CommandBuffer& cmd) {
			auto& visibleMeshlets      = State.Graph.GetPhysicalBufferResource(visibleMeshletsRes);
			auto& cullTriangleDispatch = State.Graph.GetPhysicalBufferResource(cullTriangleDispatchRes);
			auto& drawIndirect         = State.Graph.GetPhysicalBufferResource(drawIndirectRes);
			auto& meshletIndices       = State.Graph.GetPhysicalBufferResource(meshletIndicesRes);

			std::array<vk::DrawIndexedIndirectCommand, MaxMeshletBatches> indirect;
			indirect.fill(vk::DrawIndexedIndirectCommand(0, 1, 0, 0, 0));
			for (int i = 0; i < MaxMeshletBatches; ++i) { indirect[i].firstIndex = i * MaxIndicesPerBatch; }
			cmd.UpdateBuffer(drawIndirect, sizeof(indirect[0]) * indirect.size(), indirect.data());
			cmd.BufferBarrier(drawIndirect,
			                  vk::PipelineStageFlagBits2::eTransfer,
			                  vk::AccessFlagBits2::eTransferWrite,
			                  vk::PipelineStageFlagBits2::eComputeShader,
			                  vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite);

			cmd.SetProgram(
				ShaderManager::RegisterCompute("res://Shaders/CullTriangles.comp.glsl")->RegisterVariant()->GetProgram());
			cmd.SetUniformBuffer(0, 0, State.SceneBuffer.Get());
			cmd.SetUniformBuffer(1, 0, State.ComputeUniforms.Get());
			cmd.SetStorageBuffer(1, 1, visibleMeshlets);
			cmd.SetStorageBuffer(1, 2, State.MeshletBuffer.Get());
			cmd.SetStorageBuffer(1, 3, drawIndirect);
			cmd.SetStorageBuffer(1, 4, meshletIndices);

			struct PushConstant {
				uint32_t BatchID;
				uint32_t MeshletsPerBatch;
				uint32_t IndicesPerBatch;
			};
			PushConstant pc =
				PushConstant{.BatchID = 0, .MeshletsPerBatch = MaxMeshletsPerBatch, .IndicesPerBatch = MaxIndicesPerBatch};

			for (int i = 0; i < MaxMeshletBatches; ++i) {
				pc.BatchID = i;
				cmd.PushConstants(pc);
				cmd.DispatchIndirect(cullTriangleDispatch, sizeof(vk::DispatchIndirectCommand) * pc.BatchID);

				const auto b = vk::MemoryBarrier2(vk::PipelineStageFlagBits2::eAllCommands,
				                                  vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
				                                  vk::PipelineStageFlagBits2::eAllCommands,
				                                  vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
				const vk::DependencyInfo superBarrier({}, b);
				cmd.Barrier(superBarrier);
			}
		});
	}

	AttachmentInfo main;

	{
		auto& pass = State.Graph.AddPass("Main");

		AttachmentInfo depth = main.Copy().SetFormat(State.Device->GetDefaultDepthFormat());
		pass.AddColorOutput("Main", main);
		pass.SetDepthStencilOutput("Depth", depth);
		auto& drawIndirectRes   = pass.AddIndirectInput("DrawIndirect");
		auto& meshletIndicesRes = pass.AddIndexBufferInput("MeshletIndices");

		pass.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
			if (value) { *value = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f); }
			return true;
		});
		pass.SetGetClearDepthStencil([](vk::ClearDepthStencilValue* value) -> bool {
			if (value) { *value = vk::ClearDepthStencilValue(0.0f, 0); }
			return true;
		});
		pass.SetBuildRenderPass([&](Vulkan::CommandBuffer& cmd) {
			auto& drawIndirect   = State.Graph.GetPhysicalBufferResource(drawIndirectRes);
			auto& meshletIndices = State.Graph.GetPhysicalBufferResource(meshletIndicesRes);

			cmd.SetOpaqueState();
			cmd.SetProgram(State.Program->GetProgram());
			cmd.SetCullMode(vk::CullModeFlagBits::eBack);
			cmd.SetDepthCompareOp(vk::CompareOp::eGreaterOrEqual);
			cmd.SetUniformBuffer(0, 0, State.SceneBuffer.Get());
			cmd.SetStorageBuffer(1, 1, State.MeshletBuffer.Get());
			cmd.SetStorageBuffer(1, 2, State.Scene.GetPositionBuffer());
			cmd.SetStorageBuffer(1, 3, State.Scene.GetVertexBuffer());
			cmd.SetStorageBuffer(1, 4, State.Scene.GetIndexBuffer());
			cmd.SetStorageBuffer(1, 5, State.Scene.GetTriangleBuffer());
			cmd.SetStorageBuffer(1, 6, State.TransformBuffer.Get());
			cmd.SetIndexBuffer(meshletIndices, 0, vk::IndexType::eUint32);
			cmd.DrawIndexedIndirect(drawIndirect, MaxMeshletBatches, 0);
		});
	}

	{
		auto& pass = State.Graph.AddPass("Debug Lines");

		pass.AddColorOutput("Final", main, "Main");

		pass.SetBuildRenderPass([&](Vulkan::CommandBuffer& cmd) {
			if (State.DebugLines.empty()) { return; }

			struct PushConstant {
				uint32_t DebugLineCount;
			};
			const auto pc = PushConstant{.DebugLineCount = uint32_t(State.DebugLines.size())};

			cmd.SetOpaqueState();
			cmd.SetProgram(
				ShaderManager::RegisterGraphics("res://Shaders/DebugLines.vert.glsl", "res://Shaders/DebugLines.frag.glsl")
					->RegisterVariant()
					->GetProgram());
			cmd.SetPrimitiveTopology(vk::PrimitiveTopology::eLineList);
			cmd.SetUniformBuffer(0, 0, State.SceneBuffer.Get());
			cmd.SetStorageBuffer(1, 0, State.DebugLinesBuffer.Get());
			cmd.Draw(State.DebugLines.size() * 2, 1, 0, 0);
		});
	}

	{
		Luna::AttachmentInfo uiColor;

		auto& ui = State.Graph.AddPass("UI");

		ui.AddColorOutput("UI", uiColor, "Final");

		ui.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
			if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
			return false;
		});
		ui.SetBuildRenderPass([](Vulkan::CommandBuffer& cmd) { UIManager::Render(cmd); });
	}

	State.Graph.SetBackbufferSource("UI");
	State.Graph.Bake(*State.Device);
	State.Graph.InstallPhysicalBuffers(buffers);

	// State.Graph.Log();
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

	State.FrameTimer.Update();
	const auto deltaT = State.FrameTimer.Get().AsSeconds();

	State.DebugLines.clear();

	const auto swapchainExtent = Engine::GetMainWindow()->GetSwapchain().GetExtent();
	State.Camera.SetPerspective(70.0f, 0.01f, 100.0f);
	State.Camera.SetViewport(swapchainExtent.width, swapchainExtent.height);

	if (State.CameraActive) {
		const float speed  = 3.0f * deltaT;
		glm::vec3 movement = glm::vec3(0);
		if (Input::GetKey(Key::W) == InputAction::Press) { movement += glm::vec3(0, 0, speed); }
		if (Input::GetKey(Key::S) == InputAction::Press) { movement -= glm::vec3(0, 0, speed); }
		if (Input::GetKey(Key::D) == InputAction::Press) { movement += glm::vec3(speed, 0, 0); }
		if (Input::GetKey(Key::A) == InputAction::Press) { movement -= glm::vec3(speed, 0, 0); }
		if (Input::GetKey(Key::E) == InputAction::Press) { movement += glm::vec3(0, speed, 0); }
		if (Input::GetKey(Key::Q) == InputAction::Press) { movement -= glm::vec3(0, speed, 0); }
		State.Camera.Move(movement);
	}

	State.SceneData.Projection     = State.Camera.GetProjection();
	State.SceneData.View           = State.Camera.GetView();
	State.SceneData.ViewProjection = State.SceneData.Projection * State.SceneData.View;
	State.SceneData.CameraPosition = glm::vec4(State.Camera.GetPosition(), 1.0);

	RendererUI();

	if (!State.FreezeCullFrustum) {
		State.CullFrustum = State.SceneData.ViewProjection;

		const auto& vp = State.CullFrustum;
		auto& p        = State.SceneData.FrustumPlanes;
		for (int i = 0; i < 4; ++i) { p[0][i] = vp[i][3] + vp[i][0]; }
		for (int i = 0; i < 4; ++i) { p[1][i] = vp[i][3] - vp[i][0]; }
		for (int i = 0; i < 4; ++i) { p[2][i] = vp[i][3] + vp[i][1]; }
		for (int i = 0; i < 4; ++i) { p[3][i] = vp[i][3] - vp[i][1]; }
		for (int i = 0; i < 4; ++i) { p[4][i] = vp[i][3] + vp[i][2]; }
		for (int i = 0; i < 4; ++i) { p[5][i] = vp[i][3] - vp[i][2]; }
		for (auto& plane : p) {
			plane /= glm::length(glm::vec3(plane));
			plane.w = -plane.w;
		}
	}

	State.RenderScene     = State.Scene.Flatten();
	auto& sceneBuffer     = State.SceneBuffer.Get(sizeof(SceneData));
	auto& meshletBuffer   = State.MeshletBuffer.Get(sizeof(Meshlet) * State.RenderScene.Meshlets.size());
	auto& transformBuffer = State.TransformBuffer.Get(sizeof(glm::mat4) * State.RenderScene.Transforms.size());
	sceneBuffer.WriteData(&State.SceneData, sizeof(State.SceneData));
	meshletBuffer.WriteData(State.RenderScene.Meshlets.data(), sizeof(Meshlet) * State.RenderScene.Meshlets.size());
	transformBuffer.WriteData(State.RenderScene.Transforms.data(),
	                          sizeof(glm::mat4) * State.RenderScene.Transforms.size());

	if (!State.DebugLines.empty()) {
		auto& debugLinesBuffer = State.DebugLinesBuffer.Get(sizeof(DebugLine) * State.DebugLines.size());
		debugLinesBuffer.WriteData(State.DebugLines.data(), sizeof(DebugLine) * State.DebugLines.size());
	}

	ComputeUniforms compute{.MeshletCount = uint32_t(State.RenderScene.Meshlets.size())};
	State.ComputeUniforms.Get(sizeof(compute)).WriteData(&compute, sizeof(compute));

	TaskComposer composer;
	State.Graph.SetupAttachments(*State.Device, &State.Device->GetSwapchainView());
	State.Graph.EnqueueRenderPasses(*State.Device, composer);
	composer.GetOutgoingTask()->Wait();

	Engine::GetMainWindow()->GetSwapchain().Present();
}
}  // namespace Luna
