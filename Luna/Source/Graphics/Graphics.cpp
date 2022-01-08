#include <stb_image.h>
#include <tiny_gltf.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>
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

	// Create placeholder texture.
	{
		uint32_t pixels[16];
		std::fill(pixels, pixels + 16, 0xffffffff);
		const Vulkan::InitialImageData initialImage{.Data = &pixels};
		_whiteImage = _device->CreateImage(
			Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Srgb, vk::Extent2D(4, 4), false), &initialImage);
	}

	_cameraBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer));
	_sceneBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer));

	LoadShaders();
	keyboard->OnKey() += [&](Key key, InputAction action, InputMods mods) -> bool {
		if (key == Key::F2 && action == InputAction::Press) {
			LoadShaders();
			return true;
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
	mouse->OnMoved() += [this](Vec2d pos) -> bool {
		auto engine             = Engine::Get();
		const auto deltaTime    = engine->GetFrameDelta().Seconds();
		const float sensitivity = 5.0f * deltaTime;
		if (_mouseControl) {
			_camera.Rotate(pos.y * sensitivity, -pos.x * sensitivity);
			return true;
		}

		return false;
	};

	_camera.SetPosition(glm::vec3(0, 1, 0));
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	ZoneScopedN("Graphics::Update");

	if (!BeginFrame()) { return; }

	_imgui->BeginFrame();

	// Draw our UI.
	{
		ZoneScopedN("UI Update");
		ImGui::ShowDemoWindow(nullptr);
		_scene.DrawSceneGraph();
		_onUiRender();
	}

	// Update Camera movement.
	{
		ZoneScopedN("Camera Update");
		auto engine          = Engine::Get();
		auto keyboard        = Keyboard::Get();
		const auto deltaTime = engine->GetFrameDelta().Seconds();
		_camera.Update(deltaTime);
		const float moveSpeed = 5.0f * deltaTime;
		if (keyboard->GetKey(Key::W) == InputAction::Press) { _camera.Move(_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::S) == InputAction::Press) { _camera.Move(-_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::A) == InputAction::Press) { _camera.Move(-_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::D) == InputAction::Press) { _camera.Move(_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::R) == InputAction::Press) { _camera.Move(_camera.GetUp() * moveSpeed); }
		if (keyboard->GetKey(Key::F) == InputAction::Press) { _camera.Move(-_camera.GetUp() * moveSpeed); }
	}

	// Update Camera buffer.
	{
		ZoneScopedN("Camera Uniform Update");
		const auto swapchainExtent = _swapchain->GetExtent();
		const float aspectRatio    = float(swapchainExtent.width) / float(swapchainExtent.height);
		_camera.SetAspectRatio(aspectRatio);
		CameraData cam{.Projection = _camera.GetProjection(), .View = _camera.GetView(), .Position = _camera.GetPosition()};
		auto* data = _cameraBuffer->Map();
		memcpy(data, &cam, sizeof(CameraData));
		_cameraBuffer->Unmap();
	}

	// Update Scene buffer.
	{
		ZoneScopedN("Scene Uniform Update");
		SceneData scene{.SunDirection = glm::normalize(glm::vec4(1.0f, 2.0f, 0.0f, 0.0f))};
		auto* data = _sceneBuffer->Map();
		memcpy(data, &scene, sizeof(SceneData));
		_sceneBuffer->Unmap();
	}

	struct PushConstant {
		glm::mat4 Model;
	};

	auto cmd = _device->RequestCommandBuffer();

	{
		ZoneScopedN("Main Render Pass");

		auto rpInfo = _device->GetStockRenderPass(Vulkan::StockRenderPass::Depth);
		rpInfo.ClearColors[0].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
		rpInfo.ClearDepthStencil.setDepth(1.0f);
		cmd->BeginRenderPass(rpInfo);

		const auto SetTexture = [&](uint32_t set, uint32_t binding, const TextureHandle& texture) -> void {
			const bool ready      = bool(texture) && texture->Ready;
			const bool hasTexture = ready && texture->Image;
			const bool hasSampler = ready && texture->Sampler;
			const auto& view      = hasTexture ? *texture->Image->GetView() : *_whiteImage->GetView();
			const auto sampler =
				hasSampler ? texture->Sampler : _device->RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
			cmd->SetTexture(set, binding, view, sampler);
		};

		auto& registry      = _scene.GetRegistry();
		const auto rootNode = _scene.GetRoot();

		// Render meshes
		cmd->SetOpaqueState();
		cmd->SetProgram(_program);
		cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
		cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
		cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
		cmd->SetUniformBuffer(0, 0, *_cameraBuffer);
		cmd->SetUniformBuffer(0, 1, *_sceneBuffer);
		const auto view = registry.view<MeshRenderer>();
		for (const auto& entity : view) {
			const auto& transform = registry.get<TransformComponent>(entity);
			auto& mesh            = registry.get<MeshRenderer>(entity);

			if (!mesh.Mesh->Ready) { continue; }

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

				material->Update();

				cmd->SetCullMode(material->DualSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
				SetTexture(1, 1, material->Albedo);
				SetTexture(1, 2, material->Normal);
				SetTexture(1, 3, material->PBR);
				cmd->SetUniformBuffer(1, 0, *material->DataBuffer);

				if (submesh.IndexCount > 0) {
					cmd->DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
				} else {
					cmd->Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
				}
			}
		}

		// Render environment
		if (WorldData* worldData = registry.try_get<WorldData>(rootNode)) {
			if (worldData->Environment && worldData->Environment->Ready) {
				cmd->SetOpaqueState();
				cmd->SetProgram(_programSkybox);
				cmd->SetDepthCompareOp(vk::CompareOp::eEqual);
				cmd->SetCullMode(vk::CullModeFlagBits::eFront);
				cmd->SetUniformBuffer(0, 0, *_cameraBuffer);
				cmd->SetTexture(1, 0, *worldData->Environment->Skybox->GetView(), Vulkan::StockSampler::LinearClamp);
				cmd->Draw(36);
			}
		}

		cmd->EndRenderPass();
	}

	{
		ZoneScopedN("ImGUI Render");
		_imgui->EndFrame();
		_imgui->Render(cmd);
	}

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
