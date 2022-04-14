#include <stb_image.h>
#include <tiny_gltf.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRenderer.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Scene/WorldData.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <Luna/Vulkan/Swapchain.hpp>
#include <Tracy.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

//

#include <ImGuizmo.h>

namespace Luna {
Graphics::Graphics() {
	ZoneScopedN("Graphics()");

	auto filesystem = Filesystem::Get();
	auto keyboard   = Keyboard::Get();
	auto mouse      = Mouse::Get();

	filesystem->AddSearchPath("Assets");

	const auto instanceExtensions                   = Window::Get()->GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	_context      = std::make_unique<Vulkan::Context>(instanceExtensions, deviceExtensions);
	_device       = std::make_unique<Vulkan::Device>(*_context);
	_swapchain    = std::make_unique<Vulkan::Swapchain>(*_device);
	_assetManager = std::make_unique<AssetManager>();
	_imgui        = std::make_unique<ImGuiManager>(*_device);
	_sceneImages.resize(_swapchain->GetImages().size());

	// Create placeholder textures.
	{
		constexpr uint32_t width    = 4;
		constexpr uint32_t height   = 4;
		constexpr size_t pixelCount = width * height;
		uint32_t pixels[pixelCount];

		const Vulkan::ImageCreateInfo imageCI2D = {
			.Domain        = Vulkan::ImageDomain::Physical,
			.Format        = vk::Format::eR8G8B8A8Unorm,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
			.Extent        = vk::Extent3D(width, height, 1),
			.ArrayLayers   = 1,
			.MipLevels     = 1,
			.Samples       = vk::SampleCountFlagBits::e1,
			.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
		const Vulkan::ImageCreateInfo imageCICube = {
			.Domain        = Vulkan::ImageDomain::Physical,
			.Format        = vk::Format::eR8G8B8A8Unorm,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
			.Extent        = vk::Extent3D(width, height, 1),
			.ArrayLayers   = 6,
			.MipLevels     = 1,
			.Samples       = vk::SampleCountFlagBits::e1,
			.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.Flags         = Vulkan::ImageCreateFlagBits::CreateCubeCompatible};

		Vulkan::InitialImageData initialImages[6];
		for (int i = 0; i < 6; ++i) { initialImages[i] = Vulkan::InitialImageData{.Data = &pixels}; }

		// Black images
		std::fill(pixels, pixels + pixelCount, 0x000000ff);
		_defaultImages.Black2D   = _device->CreateImage(imageCI2D, initialImages);
		_defaultImages.BlackCube = _device->CreateImage(imageCICube, initialImages);

		// Gray images
		std::fill(pixels, pixels + pixelCount, 0x808080ff);
		_defaultImages.Gray2D   = _device->CreateImage(imageCI2D, initialImages);
		_defaultImages.GrayCube = _device->CreateImage(imageCICube, initialImages);

		// Normal images
		std::fill(pixels, pixels + pixelCount, 0x8080ffff);
		_defaultImages.Normal2D = _device->CreateImage(imageCI2D, initialImages);

		// White images
		std::fill(pixels, pixels + pixelCount, 0xffffffff);
		_defaultImages.White2D   = _device->CreateImage(imageCI2D, initialImages);
		_defaultImages.WhiteCube = _device->CreateImage(imageCICube, initialImages);
	}

	_cameraBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer));
	_sceneBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer));

	LoadShaders();
	keyboard->OnKey() += [&](Key key, InputAction action, InputMods mods, bool uiCapture) -> bool {
		if (key == Key::F2 && action == InputAction::Press) {
			LoadShaders();
			return true;
		}
		if (key == Key::_1 && action == InputAction::Press) { _gizmoOp = ImGuizmo::TRANSLATE; }
		if (key == Key::_2 && action == InputAction::Press) { _gizmoOp = ImGuizmo::ROTATE; }
		if (key == Key::_3 && action == InputAction::Press) { _gizmoOp = ImGuizmo::SCALE; }
		if (key == Key::GraveAccent && action == InputAction::Press) {
			if (_gizmoMode == ImGuizmo::LOCAL) {
				_gizmoMode = ImGuizmo::WORLD;
			} else {
				_gizmoMode = ImGuizmo::LOCAL;
			}
		}

		return false;
	};
	mouse->OnButton() += [this](MouseButton button, InputAction action, InputMods mods) -> bool {
		auto mouse = Mouse::Get();
		if (button == MouseButton::Button1) {
			if (action == InputAction::Press) {
				_mouseControl = true;
				mouse->SetCursorHidden(true);
				return true;
			} else {
				_mouseControl = false;
				mouse->SetCursorHidden(false);
				return true;
			}
		}

		return false;
	};
	mouse->OnMoved() += [this](glm::dvec2 pos) -> bool {
		const float sensitivity = 0.1f;
		if (_mouseControl) {
			_camera.Rotate(pos.y * sensitivity, -pos.x * sensitivity);
			return true;
		}

		return false;
	};

	_camera.SetClipping(0.01f, 48.0f);
	_camera.SetFOV(70.0f);
	_camera.SetPosition(glm::vec3(0, 1, 0));
}

Graphics::~Graphics() noexcept {}

void Graphics::SetEditorLayout(bool enabled) {
	_editorLayout = enabled;
	_imgui->SetDockspace(_editorLayout);
}

void Graphics::Update() {
	ZoneScopedN("Graphics::Update");

	if (!BeginFrame()) { return; }

	_imgui->BeginFrame();
	auto keyboard               = Keyboard::Get();
	auto mouse                  = Mouse::Get();
	auto cmd                    = _device->RequestCommandBuffer();
	auto& registry              = _scene.GetRegistry();
	const auto rootNode         = _scene.GetRoot();
	WorldData* worldData        = registry.try_get<WorldData>(rootNode);
	const bool environmentReady = worldData && worldData->Environment && worldData->Environment->Ready;
	CameraData cameraData       = {};
	SceneData sceneData         = {};

	// Scene Drawing
	vk::Extent2D sceneExtent        = _swapchain->GetExtent();
	bool sceneWindow                = false;
	Vulkan::ImageHandle& sceneImage = _sceneImages[_swapchain->GetAcquiredIndex()];

	if (_editorLayout) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		sceneWindow =
			ImGui::Begin("Scene",
		               nullptr,
		               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
		ImGui::PopStyleVar(2);
		if (sceneWindow) { ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList()); }

		const ImVec2 windowSize = ImGui::GetWindowContentRegionMax();
		const ImVec2 windowPos  = ImGui::GetWindowPos();
		ImVec2 windowMin        = ImGui::GetWindowContentRegionMin();
		windowMin.x += windowPos.x;
		windowMin.y += windowPos.y;
		ImVec2 windowMax = ImGui::GetWindowContentRegionMax();
		windowMax.x += windowPos.x;
		windowMax.y += windowPos.y;
		sceneExtent =
			vk::Extent2D{static_cast<uint32_t>(windowMax.x - windowMin.x), static_cast<uint32_t>(windowMax.y - windowMin.y)};
		if (_mouseControl) {
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				_mouseControl = false;
				mouse->SetCursorHidden(false);
			}
		} else {
			if (ImGui::IsMouseHoveringRect(windowMin, windowMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			    ImGui::IsWindowHovered() && !ImGuizmo::IsOver()) {
				_mouseControl = true;
				mouse->SetCursorHidden(true);
			}
		}

		bool needNewSceneImage = !sceneImage;
		if (sceneImage) {
			const auto& imageCI = sceneImage->GetCreateInfo();
			if (imageCI.Extent.width != windowSize.x || imageCI.Extent.height != windowSize.y) { needNewSceneImage = true; }
		}
		if (needNewSceneImage) {
			Vulkan::ImageCreateInfo imageCI =
				Vulkan::ImageCreateInfo::RenderTarget(_swapchain->GetFormat(), vk::Extent2D(windowSize.x, windowSize.y));
			imageCI.Usage |= vk::ImageUsageFlagBits::eSampled;
			sceneImage = _device->CreateImage(imageCI);
		}

		ImGuizmo::SetRect(windowMin.x, windowMin.y, windowMax.x - windowMin.x, windowMax.y - windowMin.y);
	} else {
		ImGuizmo::SetRect(0.0f, 0.0f, sceneExtent.width, sceneExtent.height);
	}

	// Update Camera movement.
	if (_mouseControl) {
		ZoneScopedN("Camera Update");
		auto engine          = Engine::Get();
		const auto deltaTime = engine->GetFrameDelta().Seconds();
		_camera.Update(deltaTime);
		float moveSpeed = 5.0f * deltaTime;
		if (keyboard->GetKey(Key::ShiftLeft, false) == InputAction::Press) { moveSpeed *= 2.0f; }
		if (keyboard->GetKey(Key::W, false) == InputAction::Press) { _camera.Move(_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::S, false) == InputAction::Press) { _camera.Move(-_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::A, false) == InputAction::Press) { _camera.Move(-_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::D, false) == InputAction::Press) { _camera.Move(_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::R, false) == InputAction::Press) { _camera.Move(_camera.GetUp() * moveSpeed); }
		if (keyboard->GetKey(Key::F, false) == InputAction::Press) { _camera.Move(-_camera.GetUp() * moveSpeed); }
	}

	const auto now           = Time::Now().Seconds() * 0.025f;
	const float sunAngle     = glm::radians(now * 360.0f);
	const float sunRadius    = 40.0f;
	const glm::vec3 lightPos = glm::vec3(cos(sunAngle) * sunRadius, sunRadius, sin(sunAngle) * sunRadius);
	_sunPosition             = lightPos;
	_sunDirection            = glm::normalize(-_sunPosition);

	// Update Camera buffer.
	{
		ZoneScopedN("Camera Uniform Update");
		const float aspectRatio = float(sceneExtent.width) / float(sceneExtent.height);
		_camera.SetAspectRatio(aspectRatio);

		cameraData.Projection  = _camera.GetProjection();
		cameraData.View        = _camera.GetView();
		cameraData.ViewInverse = glm::inverse(_camera.GetView());
		cameraData.Position    = _camera.GetPosition();
		auto* data             = _cameraBuffer->Map();
		memcpy(data, &cameraData, sizeof(CameraData));
		_cameraBuffer->Unmap();
	}

	// Update Scene buffer.
	{
		ZoneScopedN("Scene Uniform Update");
		sceneData.SunDirection = glm::vec4(_sunDirection, 0.0f);
		sceneData.PrefilteredCubeMipLevels =
			environmentReady ? worldData->Environment->Prefiltered->GetCreateInfo().MipLevels : 0.0f;
		sceneData.Exposure        = _exposure;
		sceneData.Gamma           = _gamma;
		sceneData.IBLContribution = environmentReady ? _iblContribution : 0.0f;
		auto* data                = _sceneBuffer->Map();
		memcpy(data, &sceneData, sizeof(SceneData));
		_sceneBuffer->Unmap();
	}

	struct PushConstant {
		glm::mat4 Model;
	};

	const auto SetTexture =
		[&](
			uint32_t set, uint32_t binding, const TextureHandle& texture, const Vulkan::ImageHandle& fallback = {}) -> void {
		const auto& fallbackView = fallback ? fallback : _defaultImages.White2D;
		const bool ready         = bool(texture) && texture->Ready;
		const bool hasTexture    = ready && texture->Image;
		const bool hasSampler    = ready && texture->Sampler;
		const auto& view         = hasTexture ? *texture->Image->GetView() : *fallbackView->GetView();
		const auto sampler =
			hasSampler ? texture->Sampler : _device->RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
		cmd->SetTexture(set, binding, view, sampler);
	};

	// Main Render Pass
	{
		ZoneScopedN("Main Render Pass");
		CbZone(cmd, "Main Render Pass");
		cmd->BeginZone("Main Render Pass");

		if (_editorLayout) {
			ZoneScopedN("Attachment Transition");
			CbZone(cmd, "Attachment Transition");

			const vk::ImageMemoryBarrier barrierIn({},
			                                       vk::AccessFlagBits::eColorAttachmentWrite,
			                                       vk::ImageLayout::eUndefined,
			                                       vk::ImageLayout::eColorAttachmentOptimal,
			                                       VK_QUEUE_FAMILY_IGNORED,
			                                       VK_QUEUE_FAMILY_IGNORED,
			                                       sceneImage->GetImage(),
			                                       vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmd->Barrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, barrierIn);
		}

		// Begin
		{
			ZoneScopedN("Begin");

			auto rpInfo = _device->GetStockRenderPass(Vulkan::StockRenderPass::Depth);
			if (_editorLayout) { rpInfo.ColorAttachments[0] = sceneImage->GetView().Get(); }
			rpInfo.ClearColors[0].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
			rpInfo.ClearDepthStencil.setDepth(1.0f);
			cmd->BeginRenderPass(rpInfo);
		}

		// Render meshes
		{
			ZoneScopedN("Opaque Meshes");
			CbZone(cmd, "Opaque Meshes");
			cmd->BeginZone("Opaque Meshes");

			cmd->SetOpaqueState();
			cmd->SetProgram(_program);
			cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
			cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
			cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
			cmd->SetUniformBuffer(0, 0, *_cameraBuffer);
			cmd->SetUniformBuffer(0, 1, *_sceneBuffer);

			if (worldData->Environment && worldData->Environment->Ready) {
				cmd->SetTexture(0, 2, *worldData->Environment->Irradiance->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 3, *worldData->Environment->Prefiltered->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 4, *worldData->Environment->BrdfLut->GetView(), Vulkan::StockSampler::LinearClamp);
			} else {
				cmd->SetTexture(0, 2, *_defaultImages.BlackCube->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 3, *_defaultImages.BlackCube->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 4, *_defaultImages.Black2D->GetView(), Vulkan::StockSampler::LinearClamp);
			}

			const auto view = registry.view<MeshRenderer>();
			for (const auto& entity : view) {
				const auto& transform = registry.get<TransformComponent>(entity);
				auto& mesh            = registry.get<MeshRenderer>(entity);

				if (!transform.IsEnabled(registry) || !mesh.Mesh->Ready) { continue; }

				ZoneScopedN("Mesh");
				ZoneText(transform.Name.c_str(), transform.Name.length());
				CbZone(cmd, "Mesh");

				cmd->SetVertexBinding(
					0, *mesh.Mesh->Buffer, mesh.Mesh->PositionOffset, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
				cmd->SetVertexBinding(
					1, *mesh.Mesh->Buffer, mesh.Mesh->NormalOffset, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
				cmd->SetVertexBinding(
					2, *mesh.Mesh->Buffer, mesh.Mesh->Texcoord0Offset, sizeof(glm::vec2), vk::VertexInputRate::eVertex);
				cmd->SetIndexBuffer(*mesh.Mesh->Buffer, mesh.Mesh->IndexOffset, vk::IndexType::eUint32);
				PushConstant pc{.Model = transform.CachedGlobalTransform};
				cmd->PushConstants(&pc, 0, sizeof(pc));

				for (size_t i = 0; i < mesh.Mesh->SubMeshes.size(); ++i) {
					const auto& submesh = mesh.Mesh->SubMeshes[i];
					auto& material      = mesh.Materials[i];

					if (submesh.VertexCount == 0) { continue; }

					ZoneScopedN("Submesh");
					CbZone(cmd, "Submesh");

					material->DebugView = _pbrDebug;
					material->Update();

					cmd->SetCullMode(material->DualSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
					SetTexture(1, 1, material->Albedo, _defaultImages.White2D);
					SetTexture(1, 2, material->Normal, _defaultImages.Normal2D);
					SetTexture(1, 3, material->PBR, _defaultImages.White2D);
					SetTexture(1, 4, material->Emissive, _defaultImages.Black2D);
					cmd->SetUniformBuffer(1, 0, *material->DataBuffer);

					if (submesh.IndexCount > 0) {
						ZoneScopedN("DrawIndexed");
						cmd->DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
					} else {
						ZoneScopedN("Draw");
						cmd->Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
					}
				}
			}

			cmd->EndZone();
		}

		// Render environment
		if (_drawSkybox && environmentReady) {
			ZoneScopedN("Environment");
			CbZone(cmd, "Environment");
			cmd->BeginZone("Environment");

			struct SkyboxPC {
				float DebugView = 0.0f;
			};
			SkyboxPC pc = {.DebugView = static_cast<float>(_skyDebug)};

			cmd->SetOpaqueState();
			cmd->SetProgram(_programSkybox);
			cmd->SetDepthCompareOp(vk::CompareOp::eEqual);
			cmd->SetCullMode(vk::CullModeFlagBits::eFront);
			cmd->SetUniformBuffer(0, 0, *_cameraBuffer);
			cmd->SetTexture(1, 0, *worldData->Environment->Skybox->GetView(), Vulkan::StockSampler::LinearClamp);
			cmd->SetTexture(1, 1, *worldData->Environment->Irradiance->GetView(), Vulkan::StockSampler::LinearClamp);
			cmd->SetTexture(1, 2, *worldData->Environment->Prefiltered->GetView(), Vulkan::StockSampler::LinearClamp);
			cmd->PushConstants(&pc, 0, sizeof(pc));
			cmd->Draw(36);

			cmd->EndZone();
		}

		cmd->EndRenderPass();

		if (_editorLayout) {
			ZoneScopedN("Shader Transition");
			CbZone(cmd, "Shader Transition");

			const vk::ImageMemoryBarrier barrierOut(vk::AccessFlagBits::eColorAttachmentWrite,
			                                        vk::AccessFlagBits::eShaderRead,
			                                        vk::ImageLayout::eColorAttachmentOptimal,
			                                        vk::ImageLayout::eShaderReadOnlyOptimal,
			                                        VK_QUEUE_FAMILY_IGNORED,
			                                        VK_QUEUE_FAMILY_IGNORED,
			                                        sceneImage->GetImage(),
			                                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmd->Barrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
			             vk::PipelineStageFlagBits::eFragmentShader,
			             {},
			             {},
			             barrierOut);
		}

		cmd->EndZone();
	}

	if (_editorLayout && sceneWindow) {
		ImGui::Image(reinterpret_cast<ImTextureID>(const_cast<Vulkan::ImageView*>(sceneImage->GetView().Get())),
		             ImGui::GetWindowContentRegionMax());
	}

	// Draw our UI.
	{
		ZoneScopedN("UI Update");

		ImGui::ShowDemoWindow(nullptr);

		_scene.DrawSceneGraph();
		if (registry.valid(_scene.GetSelectedEntity())) {
			Entity selected(_scene.GetSelectedEntity());
			glm::mat4 proj = cameraData.Projection;
			proj[1][1] *= -1.0f;
			auto& transform = selected.Transform();

			glm::mat4 matrix = transform.CachedGlobalTransform;
			glm::mat4 delta  = glm::mat4(1.0f);
			// TODO: Translation/scale gizmos show the wrong axes when the model is rotated.
			if (ImGuizmo::Manipulate(glm::value_ptr(cameraData.View),
			                         glm::value_ptr(proj),
			                         _gizmoOp,
			                         _gizmoMode,
			                         glm::value_ptr(matrix),
			                         glm::value_ptr(delta),
			                         nullptr,
			                         nullptr,
			                         nullptr)) {
				glm::vec3 scale;
				glm::quat rotation;
				glm::vec3 translation;
				glm::vec3 skew;
				glm::vec4 perspective;
				glm::decompose(delta, scale, rotation, translation, skew, perspective);
				if (_gizmoOp == ImGuizmo::TRANSLATE) {
					if (registry.valid(transform.Parent)) {
						const auto& parentTransform = registry.get<TransformComponent>(transform.Parent);
						const glm::mat4 invParent   = glm::inverse(parentTransform.CachedGlobalTransform);
						transform.Position += glm::vec3(invParent * glm::vec4(translation, 0.0f));
					} else {
						transform.Position += translation;
					}
				}
				if (_gizmoOp == ImGuizmo::ROTATE) {
					selected.RotateAround(transform.Position, -rotation, TransformSpace::World);
				}
				if (_gizmoOp == ImGuizmo::SCALE) { transform.Scale *= scale; }
				transform.UpdateGlobalTransform(registry);
			}
		}

		if (_editorLayout) { ImGui::End(); }

		DrawRenderSettings();
		OnUiRender();
	}

	{
		ZoneScopedN("ImGUI Render");
		CbZone(cmd, "ImGUI Render");
		cmd->BeginZone("ImGUI Render");

		_imgui->EndFrame();
		_imgui->Render(cmd);

		cmd->EndZone();
	}

	_device->Submit(cmd);

	EndFrame();
}  // namespace Luna

bool Graphics::BeginFrame() {
	ZoneScopedN("Graphics::BeginFrame");

	_device->NextFrame();

	const auto acquired = _swapchain->AcquireNextImage();

	return acquired;
}

void Graphics::EndFrame() {
	ZoneScopedN("Graphics::EndFrame");

	_device->EndFrame();
	_swapchain->Present();
	FrameMark;
}

void Graphics::DrawRenderSettings() {
	if (ImGui::Begin("Renderer")) {
		ImGui::Checkbox("Draw Skybox", &_drawSkybox);

		static const std::vector<const char*> pbrDebugViews = {
			"Final Output", "Albedo", "Tangent", "Normal", "Diffuse", "Specular", "IBL", "Shadow", "Shadow Depth"};
		ImGui::Combo("PBR Debug View", &_pbrDebug, pbrDebugViews.data(), static_cast<int>(pbrDebugViews.size()));

		static const std::vector<const char*> skyDebugViews = {"Final Output", "Irradiance", "Prefiltered"};
		ImGui::Combo("Skybox Debug View", &_skyDebug, skyDebugViews.data(), static_cast<int>(skyDebugViews.size()));

		ImGui::DragFloat3("Sun Direction", glm::value_ptr(_sunDirection));
		ImGui::DragFloat("Exposure", &_exposure, 0.1f, 0.0f, 50.0f);
		ImGui::DragFloat("Gamma", &_gamma, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("IBL Contribution", &_iblContribution, 0.01f, 0.0f, 1.0f);

		if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
			const auto& stats = _device->GetStatistics();
			ImGui::Text("Draw Calls: %llu", stats.DrawCalls.load());
			ImGui::Text(
				"Buffers: %llu (%s)", stats.BufferCount.load(), Vulkan::FormatSize(stats.BufferMemory.load()).c_str());
			ImGui::Text("Images: %llu (%s)", stats.ImageCount.load(), Vulkan::FormatSize(stats.ImageMemory.load()).c_str());
			ImGui::Text("Render Passes: %llu", stats.RenderPassCount.load());
			ImGui::Text("Shaders: %llu", stats.ShaderCount.load());
			ImGui::Text("Programs: %llu", stats.ProgramCount.load());
		}

		if (ImGui::CollapsingHeader("Performance Queries")) {
			ImGui::Checkbox("Enable Performance Queries", &_performanceQueries);
		}

		if (ImGui::Button("Reload Shaders")) { LoadShaders(); }
	}
	ImGui::End();
}

void Graphics::LoadShaders() {
	auto filesystem = Filesystem::Get();

	// Basic
	{
		auto vert = filesystem->Read("Shaders/Basic.vert.glsl");
		auto frag = filesystem->Read("Shaders/Basic.frag.glsl");
		if (vert.has_value() && frag.has_value()) {
			auto program = _device->RequestProgram(*vert, *frag);
			if (program) { _program = program; }
		}
	}

	// Skybox
	{
		auto vert = filesystem->Read("Shaders/Skybox.vert.glsl");
		auto frag = filesystem->Read("Shaders/Skybox.frag.glsl");
		if (vert.has_value() && frag.has_value()) {
			auto program = _device->RequestProgram(*vert, *frag);
			if (program) { _programSkybox = program; }
		}
	}
}
}  // namespace Luna
