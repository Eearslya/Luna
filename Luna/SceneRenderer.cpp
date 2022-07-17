#include "SceneRenderer.hpp"

#include "Scene/CameraComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/TransformComponent.hpp"
#include "Vulkan/CommandBuffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/WSI.hpp"

using namespace Luna;

SceneRenderer::SceneRenderer(Vulkan::WSI& wsi) : _wsi(wsi) {
	_sceneImages.resize(wsi.GetImageCount());
}

SceneRenderer::~SceneRenderer() noexcept {}

Luna::Vulkan::ImageHandle& SceneRenderer::GetImage(uint32_t frameIndex) {
	return _sceneImages[frameIndex];
}

void SceneRenderer::Render(Vulkan::CommandBufferHandle& cmd, Luna::Scene& scene, uint32_t frameIndex) {
	if (frameIndex >= _sceneImages.size() || _imageSize == glm::uvec2(0)) { return; }
	auto& image = _sceneImages[frameIndex];
	if (!image) { return; }

	cmd->ImageBarrier(*image,
	                  vk::ImageLayout::eUndefined,
	                  vk::ImageLayout::eColorAttachmentOptimal,
	                  vk::PipelineStageFlagBits::eTopOfPipe,
	                  {},
	                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                  vk::AccessFlagBits::eColorAttachmentWrite);

	Vulkan::RenderPassInfo rpInfo{.ColorAttachmentCount = 1, .ClearAttachments = 1, .StoreAttachments = 1};
	rpInfo.ColorAttachments[0] = &image->GetView();
	rpInfo.ClearColors[0]      = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
	cmd->BeginRenderPass(rpInfo);

	auto cameraEntity = scene.GetMainCamera();
	if (cameraEntity) {
		auto& cCameraTransform = cameraEntity.Transform();
		auto& cCamera          = cameraEntity.GetComponent<Luna::CameraComponent>();
		cCamera.Camera.SetViewport(_imageSize);

		const auto& cameraProj = cCamera.Camera.GetProjection();
		const auto& cameraView = glm::inverse(cameraEntity.GetGlobalTransform());
	}

	cmd->EndRenderPass();

	cmd->ImageBarrier(*image,
	                  vk::ImageLayout::eColorAttachmentOptimal,
	                  vk::ImageLayout::eShaderReadOnlyOptimal,
	                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                  vk::AccessFlagBits::eColorAttachmentWrite,
	                  vk::PipelineStageFlagBits::eFragmentShader,
	                  vk::AccessFlagBits::eShaderRead);
}

void SceneRenderer::SetImageSize(const glm::uvec2& size) {
	if (size != _imageSize) {
		_imageSize = size;
		_sceneImages.clear();

		Vulkan::ImageCreateInfo imageCI =
			Vulkan::ImageCreateInfo::RenderTarget(_imageSize.x, _imageSize.y, vk::Format::eB8G8R8A8Unorm);
		imageCI.Usage |= vk::ImageUsageFlagBits::eSampled;

		const auto imageCount = _wsi.GetImageCount();
		for (int i = 0; i < imageCount; ++i) { _sceneImages.push_back(_wsi.GetDevice().CreateImage(imageCI)); }
	}
}
