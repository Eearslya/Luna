#include <stb_image.h>
#include <tiny_gltf.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Devices/Keyboard.hpp>
#include <Luna/Devices/Mouse.hpp>
#include <Luna/Devices/Window.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/Light.hpp>
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
Graphics* Graphics::_instance = nullptr;

Graphics::Graphics() {
	if (_instance) { throw std::runtime_error("Graphics was initialized more than once!"); }
	_instance = this;

	ZoneScopedN("Graphics()");

	// Grab a few engine module pointers for convenience.
	auto filesystem = Filesystem::Get();
	auto keyboard   = Keyboard::Get();
	auto mouse      = Mouse::Get();

	// Add the Assets folder to allow us to load any core assets.
	filesystem->AddSearchPath("Assets");

	// Initialize our graphics device.
	const auto instanceExtensions                   = Window::Get()->GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	_context = std::make_unique<Vulkan::Context>(instanceExtensions, deviceExtensions);
	_device  = std::make_unique<Vulkan::Device>(*_context);

	// Initialize our helper classes.
	_assetManager = std::make_unique<AssetManager>();
	_imgui        = std::make_unique<ImGuiManager>(*_device);
	_imgui->SetDockspace(true);  // TODO: ImGuiManager should maybe not be responsible for this.

	// Initialize our swapchain.
	_swapchain                     = std::make_unique<Vulkan::Swapchain>(*_device);
	const auto swapchainImageCount = _swapchain->GetImages().size();
	_sceneImages.resize(swapchainImageCount);

	// Create placeholder textures.
	{
		// All textures will be 4x4 to allow for minimum texel size.
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
		std::fill(pixels, pixels + pixelCount, 0xff000000);
		_defaultImages.Black2D   = _device->CreateImage(imageCI2D, initialImages);
		_defaultImages.BlackCube = _device->CreateImage(imageCICube, initialImages);

		// Gray images
		std::fill(pixels, pixels + pixelCount, 0xff808080);
		_defaultImages.Gray2D   = _device->CreateImage(imageCI2D, initialImages);
		_defaultImages.GrayCube = _device->CreateImage(imageCICube, initialImages);

		// Normal images
		std::fill(pixels, pixels + pixelCount, 0xffff8080);
		_defaultImages.Normal2D = _device->CreateImage(imageCI2D, initialImages);

		// White images
		std::fill(pixels, pixels + pixelCount, 0xffffffff);
		_defaultImages.White2D   = _device->CreateImage(imageCI2D, initialImages);
		_defaultImages.WhiteCube = _device->CreateImage(imageCICube, initialImages);
	}

	// Create our global uniform buffers.
	// TODO: Combine into 1 global buffer? Or at least 1 buffer per type, rather than 1 per type per frame.
	for (size_t i = 0; i < swapchainImageCount; ++i) {
		_cameraBuffers.emplace_back(_device->CreateBuffer(Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Host, sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer)));
		_sceneBuffers.emplace_back(_device->CreateBuffer(Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer)));
		_lightsBuffers.emplace_back(_device->CreateBuffer(Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Host, sizeof(LightsData), vk::BufferUsageFlagBits::eUniformBuffer)));
	}

	// Load our core shaders.
	LoadShaders();

	// Set up keyboard controls.
	keyboard->OnKey() += [&](Key key, InputAction action, InputMods mods, bool uiCapture) -> bool {
		// Ignore keyboard controls when editing text with ImGui.
		if (!uiCapture) {
			if (key == Key::F5 && action == InputAction::Press) {
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
		}

		return false;
	};

	// Set up mouse controls.
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

	// Set up our default camera.
	_camera.SetClipping(0.01f, 48.0f);
	_camera.SetFOV(70.0f);
	_camera.SetPosition(glm::vec3(0, 1, 0));
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	ZoneScopedN("Graphics::Update");

	// Call BeginFrame first to determine if we should even render this frame.
	if (!BeginFrame()) { return; }

	// Start our ImGui frame.
	_imgui->BeginFrame();

	// Allocate our primary command buffer for this frame.
	auto cmd = _device->RequestCommandBuffer();

	// Prefetch a handful of convenient pointers/references.
	auto engine          = Engine::Get();                          // Used for delta time calculations.
	auto keyboard        = Keyboard::Get();                        // Used for camera keyboard movement.
	auto mouse           = Mouse::Get();                           // Used for camera mouse movement.
	auto& registry       = _scene.GetRegistry();                   // The scene we want to render.
	const auto rootNode  = _scene.GetRoot();                       // Convenience copy of the root node ID.
	WorldData* worldData = registry.try_get<WorldData>(rootNode);  // Environment data needed for rendering the scene.
	const bool environmentReady =
		worldData && worldData->Environment &&
		worldData->Environment->Ready;  // Determine whether the environment data is currently usable.
	const auto deltaTime = engine->GetFrameDelta().Seconds();  // Time in seconds since our last render.

	// Prepare our global uniform buffer data.
	CameraData cameraData = {};
	SceneData sceneData   = {};
	LightsData lightsData = {};

	// Scene Drawing
	vk::Extent2D sceneExtent        = _swapchain->GetExtent();
	bool sceneWindow                = false;  // Determines whether the scene viewport should be drawn.
	const auto swapchainIndex       = _swapchain->GetAcquiredIndex();
	Vulkan::ImageHandle& sceneImage = _sceneImages[swapchainIndex];

	// Set up our editor layout.
	// TODO: This does not belong in the Graphics class.
	{
		// Create our main "Scene" viewport.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		sceneWindow =
			ImGui::Begin("Scene",
		               nullptr,
		               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		                 ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar(2);

		// Calculate the properties of our scene window.
		const ImVec2 windowSize = ImGui::GetWindowContentRegionMax();
		const ImVec2 windowPos  = ImGui::GetWindowPos();
		ImVec2 windowMin        = ImGui::GetWindowContentRegionMin();
		windowMin.x += windowPos.x;
		windowMin.y += windowPos.y;
		ImVec2 windowMax = ImGui::GetWindowContentRegionMax();
		windowMax.x += windowPos.x;
		windowMax.y += windowPos.y;

		// Set our scene's extent to be the size of our scene window.
		sceneExtent =
			vk::Extent2D{static_cast<uint32_t>(windowMax.x - windowMin.x), static_cast<uint32_t>(windowMax.y - windowMin.y)};

		// Handle mouse camera control.
		if (_mouseControl) {
			// Note: We cannot use ImGui to determine if the mouse button is released here, because setting the cursor as
			// hidden disables all ImGui mouse input.
			if (mouse->GetButton(MouseButton::Right) == InputAction::Release) {
				_mouseControl = false;
				mouse->SetCursorHidden(false);
			}
		} else {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered() && !ImGuizmo::IsOver()) {
				_mouseControl = true;
				mouse->SetCursorHidden(true);
			}
		}

		// If necessary, (re)generate our scene backbuffer image. This happens on first draw, or when the viewport is
		// resized.
		const bool sceneImageExists = bool(sceneImage);
		bool sceneImageNeedsResize  = false;
		if (sceneImageExists) {
			const auto& imageCI = sceneImage->GetCreateInfo();
			if (imageCI.Extent.width != sceneExtent.width || imageCI.Extent.height != sceneExtent.height) {
				sceneImageNeedsResize = true;
			}
		}
		const bool needNewSceneImage = !sceneImageExists || sceneImageNeedsResize;
		if (needNewSceneImage) {
			Vulkan::ImageCreateInfo imageCI = Vulkan::ImageCreateInfo::RenderTarget(vk::Format::eR8G8B8A8Srgb, sceneExtent);
			imageCI.Usage |= vk::ImageUsageFlagBits::eSampled;
			sceneImage = _device->CreateImage(imageCI);
		}

		// Set ImGuizmo to draw within this window.
		if (sceneWindow) {
			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
			ImGuizmo::SetRect(windowMin.x, windowMin.y, windowMax.x - windowMin.x, windowMax.y - windowMin.y);
		}
	}

	// Update Camera keyboard movement. Mouse movement is handled by Delegate callback defined in the constructor.
	_camera.Update(deltaTime);
	if (_mouseControl) {
		// Allow holding Shift for faster move speed.
		// TODO: Configurable move speed.
		float moveSpeed = 5.0f * deltaTime;
		if (keyboard->GetKey(Key::ShiftLeft, false) == InputAction::Press) { moveSpeed *= 2.0f; }

		if (keyboard->GetKey(Key::W, false) == InputAction::Press) { _camera.Move(_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::S, false) == InputAction::Press) { _camera.Move(-_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::A, false) == InputAction::Press) { _camera.Move(-_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::D, false) == InputAction::Press) { _camera.Move(_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::Space, false) == InputAction::Press) { _camera.Move(_camera.GetUp() * moveSpeed); }
		if (keyboard->GetKey(Key::C, false) == InputAction::Press) { _camera.Move(-_camera.GetUp() * moveSpeed); }
	}

	// TODO: Temporary code to orbit the sun light around the scene.
	{
		const auto now           = Time::Now().Seconds() * 0.025f;
		const float sunAngle     = glm::radians(now * 360.0f);
		const float sunRadius    = 40.0f;
		const glm::vec3 lightPos = glm::vec3(cos(sunAngle) * sunRadius, sunRadius, sin(sunAngle) * sunRadius);
		_sunPosition             = lightPos;
		_sunDirection            = glm::normalize(-_sunPosition);
	}

	// Update Camera uniform buffer.
	auto& cameraBuffer = _cameraBuffers[swapchainIndex];
	{
		ZoneScopedN("Camera Uniform Update");

		const float aspectRatio = float(sceneExtent.width) / float(sceneExtent.height);
		_camera.SetAspectRatio(aspectRatio);

		cameraData.Projection  = _camera.GetProjection();
		cameraData.View        = _camera.GetView();
		cameraData.ViewInverse = glm::inverse(_camera.GetView());
		cameraData.Position    = _camera.GetPosition();

		auto* data = cameraBuffer->Map();
		memcpy(data, &cameraData, sizeof(CameraData));
		cameraBuffer->Unmap();
	}

	// Update Scene uniform buffer.
	auto& sceneBuffer = _sceneBuffers[swapchainIndex];
	{
		ZoneScopedN("Scene Uniform Update");

		sceneData.SunDirection = glm::vec4(_sunDirection, 0.0f);
		sceneData.PrefilteredCubeMipLevels =
			environmentReady ? worldData->Environment->Prefiltered->GetCreateInfo().MipLevels : 0.0f;
		sceneData.Exposure        = _exposure;
		sceneData.Gamma           = _gamma;
		sceneData.IBLContribution = environmentReady ? _iblContribution : 0.0f;

		auto* data = sceneBuffer->Map();
		memcpy(data, &sceneData, sizeof(SceneData));
		sceneBuffer->Unmap();
	}

	// Update Lights uniform buffer.
	auto& lightsBuffer = _lightsBuffers[swapchainIndex];
	{
		ZoneScopedN("Lights Uniform Update");

		auto lights    = registry.view<TransformComponent, Light>();
		int lightCount = 0;
		for (const auto& entity : lights) {
			const auto& transform                  = lights.get<TransformComponent>(entity);
			const auto& light                      = lights.get<Light>(entity);
			lightsData.Lights[lightCount].Position = glm::vec4(transform.CachedGlobalPosition, 1.0f);
			lightsData.Lights[lightCount].Color    = glm::vec4(light.Color, 1.0f);

			if (++lightCount >= 32) { break; }
		}
		lightsData.LightCount = lightCount;

		auto* data = lightsBuffer->Map();
		memcpy(data, &lightsData, sizeof(LightsData));
		lightsBuffer->Unmap();
	}

	// TODO: UBO or SSBO to store node transforms instead of push constant.
	struct PushConstant {
		glm::mat4 Model;
	};

	// Convenience function. Binds a texture descriptor, or if the texture has not loaded yet, binds a fallback texture.
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

	// Transition our scene backbuffer to be ready for rendering.
	{
		ZoneScopedN("Transition to ColorAttachment");
		CbZone(cmd, "Transition to ColorAttachment");

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

	// GBuffer Pass
	{
		// Begin
		{
			ZoneScopedN("Begin");

			Vulkan::RenderPassInfo::SubpassInfo gBufferPass  = {};
			Vulkan::RenderPassInfo::SubpassInfo lightingPass = {};
			Vulkan::RenderPassInfo::SubpassInfo gammaPass    = {};
			gBufferPass.DSUsage                              = Vulkan::DepthStencilUsage::ReadWrite;
			lightingPass.DSUsage                             = Vulkan::DepthStencilUsage::ReadOnly;

			Vulkan::RenderPassInfo info = {};

			info.ColorAttachments[info.ColorAttachmentCount] = &(*sceneImage->GetView());
			if (!_drawSkybox) {
				info.ClearAttachments |= 1 << info.ColorAttachmentCount;
				info.ClearColors[info.ColorAttachmentCount].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
			}
			info.StoreAttachments |= 1 << info.ColorAttachmentCount;
			gammaPass.ColorAttachments[gammaPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			gammaPass.ColorAttachmentCount++;
			info.ColorAttachmentCount++;

			auto hdr = _device->RequestTransientAttachment(sceneExtent, vk::Format::eB10G11R11UfloatPack32, 0);
			info.ColorAttachments[info.ColorAttachmentCount]                 = &(*hdr->GetView());
			lightingPass.ColorAttachments[lightingPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			lightingPass.ColorAttachmentCount++;
			gammaPass.InputAttachments[gammaPass.InputAttachmentCount] = info.ColorAttachmentCount;
			gammaPass.InputAttachmentCount++;
			info.ColorAttachmentCount++;

			auto position = _device->RequestTransientAttachment(sceneExtent, vk::Format::eR32G32B32A32Sfloat, 1);
			info.ColorAttachments[info.ColorAttachmentCount] = &(*position->GetView());
			info.ClearAttachments |= 1 << info.ColorAttachmentCount;
			info.ClearColors[info.ColorAttachmentCount].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
			gBufferPass.ColorAttachments[gBufferPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			gBufferPass.ColorAttachmentCount++;
			lightingPass.InputAttachments[lightingPass.InputAttachmentCount] = info.ColorAttachmentCount;
			lightingPass.InputAttachmentCount++;
			info.ColorAttachmentCount++;

			auto normal = _device->RequestTransientAttachment(sceneExtent, vk::Format::eR32G32B32A32Sfloat, 2);
			info.ColorAttachments[info.ColorAttachmentCount] = &(*normal->GetView());
			info.ClearAttachments |= 1 << info.ColorAttachmentCount;
			info.ClearColors[info.ColorAttachmentCount].setFloat32({0.5f, 0.5f, 1.0f, 1.0f});
			gBufferPass.ColorAttachments[gBufferPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			gBufferPass.ColorAttachmentCount++;
			lightingPass.InputAttachments[lightingPass.InputAttachmentCount] = info.ColorAttachmentCount;
			lightingPass.InputAttachmentCount++;
			info.ColorAttachmentCount++;

			auto albedo = _device->RequestTransientAttachment(sceneExtent, vk::Format::eR8G8B8A8Srgb, 3);
			info.ColorAttachments[info.ColorAttachmentCount] = &(*albedo->GetView());
			info.ClearAttachments |= 1 << info.ColorAttachmentCount;
			info.ClearColors[info.ColorAttachmentCount].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
			gBufferPass.ColorAttachments[gBufferPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			gBufferPass.ColorAttachmentCount++;
			lightingPass.InputAttachments[lightingPass.InputAttachmentCount] = info.ColorAttachmentCount;
			lightingPass.InputAttachmentCount++;
			info.ColorAttachmentCount++;

			auto pbr = _device->RequestTransientAttachment(sceneExtent, vk::Format::eR32G32B32A32Sfloat, 4);
			info.ColorAttachments[info.ColorAttachmentCount] = &(*pbr->GetView());
			info.ClearAttachments |= 1 << info.ColorAttachmentCount;
			info.ClearColors[info.ColorAttachmentCount].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
			gBufferPass.ColorAttachments[gBufferPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			gBufferPass.ColorAttachmentCount++;
			lightingPass.InputAttachments[lightingPass.InputAttachmentCount] = info.ColorAttachmentCount;
			lightingPass.InputAttachmentCount++;
			info.ColorAttachmentCount++;

			auto emissive = _device->RequestTransientAttachment(sceneExtent, vk::Format::eR8G8B8A8Srgb, 5);
			info.ColorAttachments[info.ColorAttachmentCount] = &(*emissive->GetView());
			info.ClearAttachments |= 1 << info.ColorAttachmentCount;
			info.ClearColors[info.ColorAttachmentCount].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
			gBufferPass.ColorAttachments[gBufferPass.ColorAttachmentCount] = info.ColorAttachmentCount;
			gBufferPass.ColorAttachmentCount++;
			lightingPass.InputAttachments[lightingPass.InputAttachmentCount] = info.ColorAttachmentCount;
			lightingPass.InputAttachmentCount++;
			info.ColorAttachmentCount++;

			auto depth = _device->RequestTransientAttachment(sceneExtent, _device->GetDefaultDepthFormat(), 6);
			info.DSOps |= Vulkan::DepthStencilOpBits::ClearDepthStencil;
			info.DepthStencilAttachment = &(*depth->GetView());
			info.ClearDepthStencil.setDepth(1.0f);

			info.Subpasses.push_back(gBufferPass);
			info.Subpasses.push_back(lightingPass);
			info.Subpasses.push_back(gammaPass);

			cmd->BeginRenderPass(info);
		}

		// Render meshes
		{
			ZoneScopedN("GBuffer Render Pass");
			CbZone(cmd, "GBuffer Render Pass");
			cmd->BeginZone("GBuffer Render Pass", glm::vec3(0.8f, 0.3f, 0.3f));

			cmd->SetOpaqueState();
			cmd->SetProgram(_programGBuffer);
			cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
			cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
			cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
			cmd->SetUniformBuffer(0, 0, *cameraBuffer);
			cmd->SetUniformBuffer(0, 1, *sceneBuffer);
			cmd->SetUniformBuffer(0, 2, *lightsBuffer);
			if (worldData->Environment && worldData->Environment->Ready) {
				cmd->SetTexture(0, 3, *worldData->Environment->Irradiance->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 4, *worldData->Environment->Prefiltered->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 5, *worldData->Environment->BrdfLut->GetView(), Vulkan::StockSampler::LinearClamp);
			} else {
				cmd->SetTexture(0, 3, *_defaultImages.BlackCube->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 4, *_defaultImages.BlackCube->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(0, 5, *_defaultImages.Black2D->GetView(), Vulkan::StockSampler::LinearClamp);
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

		cmd->NextSubpass();

		// Render lighting
		{
			ZoneScopedN("Lighting Pass");
			CbZone(cmd, "Lighting Pass");
			cmd->BeginZone("Lighting Pass", glm::vec3(0.3f, 0.3f, 0.8f));

			cmd->SetOpaqueState();
			cmd->SetCullMode(vk::CullModeFlagBits::eFront);
			cmd->SetDepthWrite(false);
			cmd->SetProgram(_programDeferred);
			cmd->SetInputAttachments(1, 0);
			cmd->Draw(3);

			cmd->EndZone();
		}

		// Render Skybox
		{
			if (_drawSkybox && environmentReady) {
				ZoneScopedN("Skybox");
				CbZone(cmd, "Skybox");
				cmd->BeginZone("Skybox", glm::vec3(0.0f, 1.0f, 1.0f));
				struct SkyboxPC {
					float DebugView = 0.0f;
				};
				SkyboxPC pc = {.DebugView = static_cast<float>(_skyDebug)};

				cmd->SetOpaqueState();
				cmd->SetProgram(_programSkybox);
				cmd->SetDepthCompareOp(vk::CompareOp::eLessOrEqual);
				cmd->SetDepthWrite(false);
				cmd->SetCullMode(vk::CullModeFlagBits::eFront);
				cmd->SetUniformBuffer(0, 0, *cameraBuffer);
				cmd->SetTexture(1, 0, *worldData->Environment->Skybox->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(1, 1, *worldData->Environment->Irradiance->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->SetTexture(1, 2, *worldData->Environment->Prefiltered->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->PushConstants(&pc, 0, sizeof(pc));
				cmd->Draw(36);

				cmd->EndZone();
			}
		}

		cmd->NextSubpass();

		// Gamma Correction
		{
			ZoneScopedN("Gamma Pass");
			CbZone(cmd, "Gamma Pass");
			cmd->BeginZone("Gamma Pass", glm::vec3(0.8f, 0.8f, 0.8f));

			cmd->SetOpaqueState();
			cmd->SetCullMode(vk::CullModeFlagBits::eFront);
			cmd->SetDepthWrite(false);
			cmd->SetProgram(_programGamma);
			cmd->SetInputAttachments(1, 0);
			cmd->Draw(3);

			cmd->EndZone();
		}

		cmd->EndRenderPass();
	}

	// Transition our scene backbuffer to be ready for sampling.
	{
		ZoneScopedN("Transition to ShaderReadOnly");
		CbZone(cmd, "Transition to ShaderReadOnly");

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

	// Draw the scene image to the ImGui window
	if (sceneWindow) {
		ImGui::Image(reinterpret_cast<ImTextureID>(const_cast<Vulkan::ImageView*>(sceneImage->GetView().Get())),
		             ImGui::GetWindowContentRegionMax());
	}

	// Draw our UI.
	{
		ZoneScopedN("UI Update");

		ImGui::ShowDemoWindow(nullptr);

		// Render Hierarchy panel.
		_scene.DrawSceneGraph();

		// Render our transform gizmo, if applicable
		if (registry.valid(_scene.GetSelectedEntity())) {
			Entity selected(_scene.GetSelectedEntity());

			// Re-flip our already-flipped projection matrix, because ImGuizmo expects OpenGL-style NDC.
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
				} else if (_gizmoOp == ImGuizmo::ROTATE) {
					selected.RotateAround(transform.Position, -rotation, TransformSpace::World);
				} else if (_gizmoOp == ImGuizmo::SCALE) {
					transform.Scale *= scale;
				}

				transform.UpdateGlobalTransform(registry);
			}
		}

		// End our "Scene" viewport window.
		ImGui::End();

		// Draw our Renderer settings panel.
		DrawRenderSettings();

		// Call our Delegate to allow other code to render UI as well.
		OnUiRender();
	}

	// Final ImGui render to swapchain.
	{
		ZoneScopedN("ImGUI Render");
		CbZone(cmd, "ImGUI Render");
		cmd->BeginZone("ImGUI Render");

		_imgui->EndFrame();
		_imgui->Render(cmd, swapchainIndex);

		cmd->EndZone();
	}

	// Submit it all.
	_device->Submit(cmd);

	EndFrame();
}

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

	// GBuffer
	{
		auto vert = filesystem->Read("Shaders/GBuffer.vert.glsl");
		auto frag = filesystem->Read("Shaders/GBuffer.frag.glsl");
		if (vert.has_value() && frag.has_value()) {
			auto program = _device->RequestProgram(*vert, *frag);
			if (program) { _programGBuffer = program; }
		}
	}

	// Deferred Lighting
	{
		auto vert = filesystem->Read("Shaders/Deferred.vert.glsl");
		auto frag = filesystem->Read("Shaders/Deferred.frag.glsl");
		if (vert.has_value() && frag.has_value()) {
			auto program = _device->RequestProgram(*vert, *frag);
			if (program) { _programDeferred = program; }
		}
	}

	// Gamma correction
	{
		auto vert = filesystem->Read("Shaders/Deferred.vert.glsl");
		auto frag = filesystem->Read("Shaders/Gamma.frag.glsl");
		if (vert.has_value() && frag.has_value()) {
			auto program = _device->RequestProgram(*vert, *frag);
			if (program) { _programGamma = program; }
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
