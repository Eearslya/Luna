#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/Camera.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Scene.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Renderer/UIManager.hpp>
#include <Luna/Utility/Math.hpp>
#include <Luna/Utility/Time.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
constexpr static unsigned int MaxMeshlets          = 262'144;
constexpr static unsigned int MaxMeshletsPerBatch  = 65'536;
constexpr static unsigned int MaxMeshletBatches    = MaxMeshlets / MaxMeshletsPerBatch;
constexpr static unsigned int MaxTrianglesPerBatch = MaxMeshletsPerBatch * 64;
constexpr static unsigned int MaxIndicesPerBatch   = MaxTrianglesPerBatch * 3;

enum class CullFlagBits : uint32_t {
	MeshletFrustum   = 1 << 0,
	MeshletHiZ       = 1 << 1,
	TriangleBackface = 1 << 2,
};
template <>
struct EnableBitmaskOperators<CullFlagBits> : std::true_type {};
using CullFlags = Bitmask<CullFlagBits>;

struct PerFrameBuffer {
	Vulkan::Buffer& Get(vk::DeviceSize size = vk::WholeSize) {
		const auto frameIndex = Renderer::GetDevice().GetFrameIndex();
		if (frameIndex >= Buffers.size()) { Buffers.resize(frameIndex + 1); }

		if (!Buffers[frameIndex] || (size != vk::WholeSize && Buffers[frameIndex]->GetCreateInfo().Size < size)) {
			const Vulkan::BufferCreateInfo bufferCI(Vulkan::BufferDomain::Host, size);
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
	glm::vec2 ViewportExtent;
};

struct ComputeUniforms {
	uint32_t CullingFlags     = 0;
	uint32_t MeshletCount     = 0;
	uint32_t MeshletsPerBatch = MaxMeshletsPerBatch;
	uint32_t IndicesPerBatch  = 0;
};

struct VisbufferStats {
	uint32_t VisibleMeshlets;
	uint32_t VisibleTriangles;
};

struct DebugLine {
	glm::vec3 Start;
	glm::vec3 End;
	glm::vec3 Color;
};

struct RenderResources {
	~RenderResources() noexcept {
		VisibleMeshlets.Reset();
		CullTriangleDispatch.Reset();
		DrawIndirect.Reset();
		MeshletIndices.Reset();
		VisBufferStats.Reset();
		DepthBuffer.Reset();
		HiZBuffer.Reset();
		HiZBufferMips.clear();
	}

	Vulkan::BufferHandle VisibleMeshlets;
	Vulkan::BufferHandle CullTriangleDispatch;
	Vulkan::BufferHandle DrawIndirect;
	Vulkan::BufferHandle MeshletIndices;
	Vulkan::BufferHandle VisBufferStats;
	Vulkan::ImageHandle DepthBuffer;
	Vulkan::ImageHandle HiZBuffer;
	std::vector<Vulkan::ImageViewHandle> HiZBufferMips;

	ShaderProgramVariant* CullMeshlets  = nullptr;
	ShaderProgramVariant* CullTriangles = nullptr;
	ShaderProgramVariant* HzbCopy       = nullptr;
	ShaderProgramVariant* HzbReduce     = nullptr;
};

static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
	Hash LastSwapchainHash = 0;

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
	PerFrameBuffer VisBufferStatsBuffer;
	RenderResources Resources;

	std::vector<DebugLine> DebugLines;
	SceneData SceneData;
	glm::mat4 CullFrustum = glm::mat4(1.0f);

	bool FreezeCullFrustum = false;
	bool ShowCullFrustum   = false;

	bool CullMeshletsFrustum   = true;
	bool CullMeshletsHiZ       = true;
	bool CullTrianglesBackface = true;
} State;

static void DrawDebugLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color = glm::vec3(1)) {
	State.DebugLines.emplace_back(start, end, color);
}

static void RendererUI() {
	const auto& statsBuffer = State.VisBufferStatsBuffer.Get();
	const auto* stats       = statsBuffer.Map<VisbufferStats>();

	if (ImGui::Begin("Model")) {
		ImGui::TableNextColumn();
		ImGui::Checkbox("Freeze Culling Frustum", &State.FreezeCullFrustum);
		ImGui::Checkbox("Show Culling Frustum", &State.ShowCullFrustum);

		ImGui::Spacing();

		ImGui::Checkbox("Meshlet Frustum Cull", &State.CullMeshletsFrustum);
		ImGui::Checkbox("Meshlet Occlusion Cull", &State.CullMeshletsHiZ);
		ImGui::Checkbox("Triangle Backface Cull", &State.CullTrianglesBackface);

		const double visibleMeshlets(stats->VisibleMeshlets);
		const double totalMeshlets(State.RenderScene.Meshlets.size());
		const double culledMeshletsPct = std::floor((1.0 - (visibleMeshlets / totalMeshlets)) * 100.0);
		const double visibleTriangles(stats->VisibleTriangles);
		const double totalTriangles(State.RenderScene.TriangleCount);
		const double culledTrianglesPct = std::floor((1.0 - (visibleTriangles / totalTriangles)) * 100.0);
		ImGui::Text("Meshlets: %u / %u (%.0f%% culled)",
		            stats->VisibleMeshlets,
		            uint32_t(State.RenderScene.Meshlets.size()),
		            culledMeshletsPct);
		ImGui::Text("Triangles: %u / %llu (%.0f%% culled)",
		            stats->VisibleTriangles,
		            State.RenderScene.TriangleCount,
		            culledTrianglesPct);

		ImGui::Spacing();

		auto meshletCullReport = State.Device->GetTimestampReport("Cull Meshlets");
		ImGui::Text("Meshlet Cull: %.2fms", meshletCullReport.TimePerFrameContext * 1'000.0);
		auto triangleCullReport = State.Device->GetTimestampReport("Cull Triangles");
		ImGui::Text("Triangle Cull: %.2fms", triangleCullReport.TimePerFrameContext * 1'000.0);
		auto visbufferReport = State.Device->GetTimestampReport("VisBuffer");
		ImGui::Text("VisBuffer: %.2fms", visbufferReport.TimePerFrameContext * 1'000.0);
		auto hzbReport = State.Device->GetTimestampReport("Hi-Z Buffer");
		ImGui::Text("Hi-Z: %.2fms", hzbReport.TimePerFrameContext * 1'000.0);
	}
	ImGui::End();

	if (State.ShowCullFrustum) {
		const auto& invVP    = glm::inverse(State.CullFrustum);
		glm::vec3 corners[8] = {glm::vec3(-1, -1, 0),
		                        glm::vec3(1, -1, 0),
		                        glm::vec3(-1, 1, 0),
		                        glm::vec3(1, 1, 0),
		                        glm::vec3(-1, -1, 1.0f - 1e-5),
		                        glm::vec3(1, -1, 1.0f - 1e-5),
		                        glm::vec3(-1, 1, 1.0f - 1e-5),
		                        glm::vec3(1, 1, 1.0f - 1e-5)};
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

	auto& res = State.Resources;

	const Vulkan::BufferCreateInfo visibleMeshlets(Vulkan::BufferDomain::Device, MaxMeshlets * sizeof(uint32_t));
	res.VisibleMeshlets = State.Device->CreateBuffer(visibleMeshlets);

	const Vulkan::BufferCreateInfo cullTriangleDispatch(Vulkan::BufferDomain::Device,
	                                                    MaxMeshletBatches * sizeof(vk::DispatchIndirectCommand));
	res.CullTriangleDispatch = State.Device->CreateBuffer(cullTriangleDispatch);

	const Vulkan::BufferCreateInfo drawIndirect(Vulkan::BufferDomain::Device,
	                                            MaxMeshletBatches * sizeof(vk::DrawIndexedIndirectCommand));
	res.DrawIndirect = State.Device->CreateBuffer(drawIndirect);

	const Vulkan::BufferCreateInfo meshletIndices(Vulkan::BufferDomain::Device,
	                                              MaxIndicesPerBatch * MaxMeshletBatches * sizeof(uint32_t));
	res.MeshletIndices = State.Device->CreateBuffer(meshletIndices);

	const Vulkan::BufferCreateInfo visBufferStats(Vulkan::BufferDomain::Device, sizeof(VisbufferStats));
	res.VisBufferStats = State.Device->CreateBuffer(visBufferStats);

	return true;
}

void Renderer::Shutdown() {
	State.Scene.Clear();
	State.Resources.~RenderResources();
	State.VisBufferStatsBuffer.Reset();
	State.SceneBuffer.Reset();
	State.DebugLinesBuffer.Reset();
	State.ComputeUniforms.Reset();
	State.MeshletBuffer.Reset();
	State.TransformBuffer.Reset();
	State.Device.Reset();
	State.Context.Reset();
}

Vulkan::Device& Renderer::GetDevice() {
	return *State.Device;
}

void Renderer::Render() {
	auto& device = *State.Device;
	device.NextFrame();

	if (!Engine::GetMainWindow()) { return; }

	const bool acquired = Engine::GetMainWindow()->GetSwapchain().Acquire();
	if (!acquired) { return; }

	if (Engine::GetMainWindow()->GetSwapchainHash() != State.LastSwapchainHash) {
		// BakeRenderGraph();
		State.LastSwapchainHash = Engine::GetMainWindow()->GetSwapchainHash();
	}

	State.FrameTimer.Update();
	const auto deltaT = State.FrameTimer.Get().AsSeconds();

	State.DebugLines.clear();

	const auto swapchainExtent = Engine::GetMainWindow()->GetSwapchain().GetExtent();
	State.Camera.SetPerspective(70.0f, 0.01f, 100.0f);
	State.Camera.SetViewport(swapchainExtent.width, swapchainExtent.height);

	if (State.CameraActive) {
		float speed = 3.0f * deltaT;
		if (Input::GetKey(Key::ShiftLeft) == InputAction::Press) { speed *= 5.0f; }
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
	State.SceneData.ViewportExtent = glm::vec2(swapchainExtent.width, swapchainExtent.height);

	auto& statsBuffer = State.VisBufferStatsBuffer.Get(sizeof(VisbufferStats));

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

	ComputeUniforms compute{.MeshletCount    = uint32_t(State.RenderScene.Meshlets.size()),
	                        .IndicesPerBatch = MaxIndicesPerBatch};
	compute.CullingFlags = 0;
	if (State.CullMeshletsFrustum) { compute.CullingFlags |= uint32_t(CullFlagBits::MeshletFrustum); }
	if (State.CullMeshletsHiZ) { compute.CullingFlags |= uint32_t(CullFlagBits::MeshletHiZ); }
	if (State.CullTrianglesBackface) { compute.CullingFlags |= uint32_t(CullFlagBits::TriangleBackface); }
	State.ComputeUniforms.Get(sizeof(compute)).WriteData(&compute, sizeof(compute));

	auto& res = State.Resources;
	if (!res.CullMeshlets) {
		res.CullMeshlets = ShaderManager::RegisterCompute("res://Shaders/CullMeshlets.comp.glsl")->RegisterVariant();
	}
	if (!res.CullTriangles) {
		res.CullTriangles = ShaderManager::RegisterCompute("res://Shaders/CullTriangles.comp.glsl")->RegisterVariant();
	}
	if (!res.HzbCopy) {
		res.HzbCopy = ShaderManager::RegisterCompute("res://Shaders/HzbCopy.comp.glsl")->RegisterVariant();
	}
	if (!res.HzbReduce) {
		res.HzbReduce = ShaderManager::RegisterCompute("res://Shaders/HzbReduce.comp.glsl")->RegisterVariant();
	}
	// Depth Buffer
	{
		bool needDepthBuffer = true;
		if (res.DepthBuffer && res.DepthBuffer->GetCreateInfo().Width == swapchainExtent.width &&
		    res.DepthBuffer->GetCreateInfo().Height == swapchainExtent.height) {
			needDepthBuffer = false;
		}

		if (needDepthBuffer) {
			const Vulkan::ImageCreateInfo imageCI =
				Vulkan::ImageCreateInfo()
					.SetFormat(State.Device->GetDefaultDepthFormat())
					.SetExtent(swapchainExtent)
					.SetUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
			res.DepthBuffer = State.Device->CreateImage(imageCI);
		}
	}
	// HiZBuffer
	{
		const auto hzbWidth  = PreviousPowerOfTwo(swapchainExtent.width);
		const auto hzbHeight = PreviousPowerOfTwo(swapchainExtent.height);
		bool needHiZBuffer   = true;
		if (res.HiZBuffer && res.HiZBuffer->GetCreateInfo().Width == hzbWidth &&
		    res.HiZBuffer->GetCreateInfo().Height == hzbHeight) {
			needHiZBuffer = false;
		}

		if (needHiZBuffer) {
			const Vulkan::ImageCreateInfo imageCI =
				Vulkan::ImageCreateInfo()
					.SetFormat(vk::Format::eR32Sfloat)
					.SetWidth(hzbWidth)
					.SetHeight(hzbHeight)
					.SetUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage)
					.SetInitialLayout(vk::ImageLayout::eGeneral)
					.SetMipLevels(Vulkan::CalculateMipLevels(hzbWidth, hzbHeight, 1));
			res.HiZBuffer = State.Device->CreateImage(imageCI);
			res.HiZBuffer->SetLayoutType(Vulkan::ImageLayoutType::General);

			res.HiZBufferMips.clear();
			Vulkan::ImageViewCreateInfo viewCI = {.Image       = res.HiZBuffer.Get(),
			                                      .Format      = imageCI.Format,
			                                      .BaseLevel   = 0,
			                                      .MipLevels   = 1,
			                                      .BaseLayer   = 0,
			                                      .ArrayLayers = imageCI.ArrayLayers,
			                                      .ViewType    = vk::ImageViewType::e2D};
			for (uint32_t i = 0; i < imageCI.MipLevels; ++i) {
				viewCI.BaseLevel = i;
				res.HiZBufferMips.push_back(State.Device->CreateImageView(viewCI));
			}
		}
	}

	State.Program =
		ShaderManager::RegisterGraphics("res://Shaders/StaticMesh.vert.glsl", "res://Shaders/StaticMesh.frag.glsl")
			->RegisterVariant();

	const uint32_t meshletBatches = (State.RenderScene.Meshlets.size() + (MaxMeshletsPerBatch - 1)) / MaxMeshletsPerBatch;

	auto cmd = State.Device->RequestCommandBuffer();

	// Compute Prep
	{
		const vk::ImageMemoryBarrier2 imageInvalidates[]   = {vk::ImageMemoryBarrier2(
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eNone,
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eGeneral,
      vk::ImageLayout::eGeneral,
      vk::QueueFamilyIgnored,
      vk::QueueFamilyIgnored,
      res.HiZBuffer->GetImage(),
      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, vk::RemainingMipLevels, 0, 1))};
		const vk::BufferMemoryBarrier2 bufferInvalidates[] = {
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eNone,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.VisibleMeshlets->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eDrawIndirect,
		                           vk::AccessFlagBits2::eNone,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.CullTriangleDispatch->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eHost,
		                           vk::AccessFlagBits2::eNone,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.VisBufferStats->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eDrawIndirect,
		                           vk::AccessFlagBits2::eNone,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.DrawIndirect->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eNone,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.MeshletIndices->GetBuffer(),
		                           0,
		                           vk::WholeSize),
		};
		const vk::DependencyInfo invalidate({}, nullptr, bufferInvalidates, imageInvalidates);
		cmd->Barrier(invalidate);

		// =====
		// Initialize our indirect command buffers
		// =====
		std::array<vk::DispatchIndirectCommand, MaxMeshletBatches> dispatchIndirect;
		dispatchIndirect.fill(vk::DispatchIndirectCommand(0, 1, 1));
		std::array<vk::DrawIndexedIndirectCommand, MaxMeshletBatches> drawIndirect;
		drawIndirect.fill(vk::DrawIndexedIndirectCommand(0, 1, 0, 0, 0));
		for (int i = 0; i < MaxMeshletBatches; ++i) { drawIndirect[i].firstIndex = i * MaxIndicesPerBatch; }

		cmd->FillBuffer(*res.VisBufferStats, 0);
		cmd->UpdateBuffer(
			*res.CullTriangleDispatch, sizeof(dispatchIndirect[0]) * dispatchIndirect.size(), dispatchIndirect.data());
		cmd->UpdateBuffer(*res.DrawIndirect, sizeof(drawIndirect[0]) * drawIndirect.size(), drawIndirect.data());

		const vk::BufferMemoryBarrier2 bufferInitializes[] = {
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eTransfer,
		                           vk::AccessFlagBits2::eTransferWrite,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.CullTriangleDispatch->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eTransfer,
		                           vk::AccessFlagBits2::eTransferWrite,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.VisBufferStats->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eTransfer,
		                           vk::AccessFlagBits2::eTransferWrite,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.DrawIndirect->GetBuffer(),
		                           0,
		                           vk::WholeSize)};
		const vk::DependencyInfo bufferInitial({}, nullptr, bufferInitializes, nullptr);
		cmd->Barrier(bufferInitial);
	}

	// Meshlet Cull
	{
		auto start = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eComputeShader);

		// Read-Only
		cmd->SetUniformBuffer(0, 0, State.SceneBuffer.Get());
		cmd->SetUniformBuffer(1, 0, State.ComputeUniforms.Get());
		cmd->SetStorageBuffer(1, 1, State.MeshletBuffer.Get());
		cmd->SetStorageBuffer(1, 2, State.TransformBuffer.Get());
		cmd->SetTexture(1, 3, res.HiZBuffer->GetView(), Vulkan::StockSampler::LinearMin);

		// Write-Only
		cmd->SetStorageBuffer(1, 4, *res.VisibleMeshlets);
		cmd->SetStorageBuffer(1, 5, *res.CullTriangleDispatch);
		cmd->SetStorageBuffer(1, 6, *res.VisBufferStats);

		cmd->SetProgram(res.CullMeshlets->GetProgram());
		cmd->Dispatch(MaxMeshletsPerBatch, meshletBatches, 1);

		const vk::BufferMemoryBarrier2 flushes[] = {
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageRead,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.VisibleMeshlets->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::PipelineStageFlagBits2::eDrawIndirect,
		                           vk::AccessFlagBits2::eIndirectCommandRead,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.CullTriangleDispatch->GetBuffer(),
		                           0,
		                           vk::WholeSize),
		};
		const vk::DependencyInfo flush({}, nullptr, flushes, nullptr);
		cmd->Barrier(flush);

		auto end = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eComputeShader);
		State.Device->RegisterTimeInterval(start, end, "Cull Meshlets");
	}

	// Triangle Cull
	{
		auto start = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eComputeShader);

		// Read-Only
		cmd->SetUniformBuffer(0, 0, State.SceneBuffer.Get());
		cmd->SetUniformBuffer(1, 0, State.ComputeUniforms.Get());
		cmd->SetStorageBuffer(1, 1, State.MeshletBuffer.Get());
		cmd->SetStorageBuffer(1, 2, State.Scene.GetPositionBuffer());
		cmd->SetStorageBuffer(1, 3, State.Scene.GetVertexBuffer());
		cmd->SetStorageBuffer(1, 4, State.Scene.GetIndexBuffer());
		cmd->SetStorageBuffer(1, 5, State.Scene.GetTriangleBuffer());
		cmd->SetStorageBuffer(1, 6, State.TransformBuffer.Get());
		cmd->SetTexture(1, 7, res.HiZBuffer->GetView(), Vulkan::StockSampler::NearestClamp);
		cmd->SetStorageBuffer(1, 8, *res.VisibleMeshlets);

		// Write-Only
		cmd->SetStorageBuffer(1, 9, *res.DrawIndirect);
		cmd->SetStorageBuffer(1, 10, *res.MeshletIndices);
		cmd->SetStorageBuffer(1, 11, *res.VisBufferStats);

		struct PushConstant {
			uint32_t BatchID = 0;
		};
		PushConstant pc;

		cmd->SetProgram(res.CullTriangles->GetProgram());
		for (uint32_t i = 0; i < meshletBatches; ++i) {
			pc.BatchID = i;
			cmd->PushConstants(pc);
			cmd->DispatchIndirect(*res.CullTriangleDispatch, sizeof(vk::DispatchIndirectCommand) * i);
		}

		const vk::BufferMemoryBarrier2 flushes[] = {
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::PipelineStageFlagBits2::eTransfer,
		                           vk::AccessFlagBits2::eTransferRead,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.VisBufferStats->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::PipelineStageFlagBits2::eDrawIndirect,
		                           vk::AccessFlagBits2::eIndirectCommandRead,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.DrawIndirect->GetBuffer(),
		                           0,
		                           vk::WholeSize),
			vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader,
		                           vk::AccessFlagBits2::eShaderStorageWrite,
		                           vk::PipelineStageFlagBits2::eVertexInput,
		                           vk::AccessFlagBits2::eIndexRead,
		                           vk::QueueFamilyIgnored,
		                           vk::QueueFamilyIgnored,
		                           res.MeshletIndices->GetBuffer(),
		                           0,
		                           vk::WholeSize),
		};
		const vk::DependencyInfo flush({}, nullptr, flushes, nullptr);
		cmd->Barrier(flush);

		cmd->CopyBuffer(State.VisBufferStatsBuffer.Get(), *res.VisBufferStats);

		auto end = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eComputeShader);
		State.Device->RegisterTimeInterval(start, end, "Cull Triangles");
	}

	// VisBuffer Render
	{
		auto start = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eVertexShader);

		const vk::ImageMemoryBarrier2 imageInvalidates[] = {vk::ImageMemoryBarrier2(
			vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eEarlyFragmentTests |
				vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eNone,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::QueueFamilyIgnored,
			vk::QueueFamilyIgnored,
			res.DepthBuffer->GetImage(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1))};
		const vk::DependencyInfo invalidate({}, nullptr, nullptr, imageInvalidates);
		cmd->Barrier(invalidate);

		auto rpInfo  = State.Device->GetSwapchainRenderPass();
		rpInfo.Flags = Vulkan::RenderPassFlagBits::ClearDepthStencil | Vulkan::RenderPassFlagBits::StoreDepthStencil;
		rpInfo.DepthStencilAttachment = &res.DepthBuffer->GetView();
		rpInfo.ClearAttachmentMask    = 1u << 0;
		rpInfo.StoreAttachmentMask    = 1u << 0;
		rpInfo.ClearColors[0]         = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);
		rpInfo.ClearDepthStencil      = vk::ClearDepthStencilValue(0.0f, 0);
		cmd->BeginRenderPass(rpInfo);

		cmd->SetOpaqueState();
		cmd->SetProgram(State.Program->GetProgram());
		cmd->SetCullMode(vk::CullModeFlagBits::eBack);
		cmd->SetDepthCompareOp(vk::CompareOp::eGreaterOrEqual);
		cmd->SetUniformBuffer(0, 0, State.SceneBuffer.Get());
		cmd->SetStorageBuffer(1, 1, State.MeshletBuffer.Get());
		cmd->SetStorageBuffer(1, 2, State.Scene.GetPositionBuffer());
		cmd->SetStorageBuffer(1, 3, State.Scene.GetVertexBuffer());
		cmd->SetStorageBuffer(1, 4, State.Scene.GetIndexBuffer());
		cmd->SetStorageBuffer(1, 5, State.Scene.GetTriangleBuffer());
		cmd->SetStorageBuffer(1, 6, State.TransformBuffer.Get());
		cmd->SetIndexBuffer(*res.MeshletIndices, 0, vk::IndexType::eUint32);
		cmd->DrawIndexedIndirect(*res.DrawIndirect, meshletBatches, 0);

		cmd->EndRenderPass();

		auto end = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eColorAttachmentOutput);
		State.Device->RegisterTimeInterval(start, end, "VisBuffer");
	}

	// Hi-Z Buffer
	{
		auto start = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eComputeShader);

		const vk::ImageMemoryBarrier2 imageInvalidates[] = {
			vk::ImageMemoryBarrier2(
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eNone,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderStorageWrite,
				vk::ImageLayout::eGeneral,
				vk::ImageLayout::eGeneral,
				vk::QueueFamilyIgnored,
				vk::QueueFamilyIgnored,
				res.HiZBuffer->GetImage(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, vk::RemainingMipLevels, 0, 1)),
			vk::ImageMemoryBarrier2(
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
				vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderSampledRead,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::QueueFamilyIgnored,
				vk::QueueFamilyIgnored,
				res.DepthBuffer->GetImage(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)),
		};
		const vk::DependencyInfo invalidate({}, nullptr, nullptr, imageInvalidates);
		cmd->Barrier(invalidate);

		auto hzbWidth  = res.HiZBuffer->GetCreateInfo().Width;
		auto hzbHeight = res.HiZBuffer->GetCreateInfo().Height;

		cmd->SetOpaqueState();
		cmd->SetProgram(res.HzbCopy->GetProgram());

		vk::ImageMemoryBarrier2 hzbBarrier(
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageWrite,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
			vk::ImageLayout::eGeneral,
			vk::ImageLayout::eGeneral,
			vk::QueueFamilyIgnored,
			vk::QueueFamilyIgnored,
			res.HiZBuffer->GetImage(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

		for (uint32_t i = 0; i < res.HiZBuffer->GetCreateInfo().MipLevels; ++i) {
			if (i > 0) {
				hzbBarrier.subresourceRange.baseMipLevel = i - 1;
				cmd->ImageBarriers({hzbBarrier});
			}

			if (i == 0) {
				cmd->SetTexture(0, 0, res.DepthBuffer->GetView(), Vulkan::StockSampler::LinearMin);
			} else {
				cmd->SetTexture(0, 0, *res.HiZBufferMips[i - 1], Vulkan::StockSampler::LinearMin);
			}
			cmd->SetStorageTexture(0, 1, *res.HiZBufferMips[i]);
			cmd->Dispatch((hzbWidth + 15) / 16, (hzbHeight + 15) / 16, 1);

			hzbWidth  = std::max(1u, hzbWidth >> 1);
			hzbHeight = std::max(1u, hzbHeight >> 1);
		}

		hzbBarrier.subresourceRange.baseMipLevel = res.HiZBuffer->GetCreateInfo().MipLevels - 1;
		cmd->ImageBarriers({hzbBarrier});

		auto end = cmd->WriteTimestamp(vk::PipelineStageFlagBits2::eComputeShader);
		State.Device->RegisterTimeInterval(start, end, "Hi-Z Buffer");
	}

	// Debug Lines
	if (State.DebugLines.size() > 0) {
		auto rpInfo                = State.Device->GetSwapchainRenderPass();
		rpInfo.ClearAttachmentMask = 0;
		rpInfo.LoadAttachmentMask  = 1u << 0;
		rpInfo.StoreAttachmentMask = 1u << 0;
		cmd->BeginRenderPass(rpInfo);

		cmd->SetOpaqueState();
		cmd->SetProgram(
			ShaderManager::RegisterGraphics("res://Shaders/DebugLines.vert.glsl", "res://Shaders/DebugLines.frag.glsl")
				->RegisterVariant()
				->GetProgram());
		cmd->SetPrimitiveTopology(vk::PrimitiveTopology::eLineList);
		cmd->SetUniformBuffer(0, 0, State.SceneBuffer.Get());
		cmd->SetStorageBuffer(1, 0, State.DebugLinesBuffer.Get());
		cmd->Draw(State.DebugLines.size() * 2, 1, 0, 0);

		cmd->EndRenderPass();
	}

	// UI Render
	{
		auto rpInfo                = State.Device->GetSwapchainRenderPass();
		rpInfo.ClearAttachmentMask = 0;
		rpInfo.LoadAttachmentMask  = 1u << 0;
		rpInfo.StoreAttachmentMask = 1u << 0;
		cmd->BeginRenderPass(rpInfo);

		UIManager::Render(*cmd);

		cmd->EndRenderPass();
	}

	State.Device->Submit(cmd);

	Engine::GetMainWindow()->GetSwapchain().Present();
}
}  // namespace Luna
