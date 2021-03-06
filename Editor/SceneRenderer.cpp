#include "SceneRenderer.hpp"

#include <Scene/CameraComponent.hpp>
#include <Scene/Entity.hpp>
#include <Scene/MeshComponent.hpp>
#include <Scene/Scene.hpp>
#include <Scene/TransformComponent.hpp>
#include <Utility/Files.hpp>
#include <Vulkan/Buffer.hpp>
#include <Vulkan/CommandBuffer.hpp>
#include <Vulkan/Device.hpp>
#include <Vulkan/Image.hpp>
#include <Vulkan/RenderPass.hpp>
#include <Vulkan/WSI.hpp>

#include "AssetManager.hpp"

using namespace Luna;

SceneRenderer::SceneRenderer(Vulkan::WSI& wsi) : _wsi(wsi) {
	_program = wsi.GetDevice().RequestProgram(ReadFile("Assets/Shaders/Basic.vert.glsl"),
	                                          ReadFile("Assets/Shaders/Basic.frag.glsl"));
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
	auto& sceneBuffer = _sceneBuffers[frameIndex];

	cmd->ImageBarrier(*image,
	                  vk::ImageLayout::eUndefined,
	                  vk::ImageLayout::eColorAttachmentOptimal,
	                  vk::PipelineStageFlagBits::eTopOfPipe,
	                  {},
	                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                  vk::AccessFlagBits::eColorAttachmentWrite);

	auto depth = _wsi.GetDevice().RequestTransientAttachment(vk::Extent2D(_imageSize.x, _imageSize.y),
	                                                         _wsi.GetDevice().GetDefaultDepthFormat());

	Vulkan::RenderPassInfo rpInfo;
	rpInfo.ColorAttachmentCount   = 1;
	rpInfo.ColorAttachments[0]    = &image->GetView();
	rpInfo.DepthStencilAttachment = &(depth->GetView());
	rpInfo.ClearAttachments       = 1 << 0 | 1 << 1;
	rpInfo.StoreAttachments       = 1 << 0;
	rpInfo.ClearColors[0]         = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
	rpInfo.ClearDepthStencil      = vk::ClearDepthStencilValue(1.0f, 0);
	cmd->BeginRenderPass(rpInfo);

#if 0
	auto cameraEntity = scene.GetMainCamera();
	if (cameraEntity) {
		auto& cCameraTransform = cameraEntity.Transform();
		auto& cCamera          = cameraEntity.GetComponent<Luna::CameraComponent>();
		cCamera.Camera.SetViewport(_imageSize);

		const auto& cameraProj = cCamera.Camera.GetProjection();
		const auto& cameraView = glm::inverse(cameraEntity.GetGlobalTransform());
		SceneData* sceneData   = reinterpret_cast<SceneData*>(sceneBuffer->Map());
		sceneData->Projection  = cameraProj;
		sceneData->View        = cameraView;

		cmd->SetProgram(_program);
		cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
		cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
		cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
		cmd->SetUniformBuffer(0, 0, *sceneBuffer);
		auto renderables = scene.GetRegistry().view<MeshComponent>();
		for (auto entityId : renderables) {
			Entity entity(entityId, scene);
			const auto& cMesh = renderables.get<MeshComponent>(entityId);
			const PushConstant pc{.Model = entity.GetGlobalTransform()};
			cmd->PushConstants(&pc, 0, sizeof(PushConstant));

			Mesh* mesh = AssetManager::GetMesh(cMesh.MeshAssetPath);
			if (mesh) {
				const auto& submesh = mesh->Submeshes[cMesh.SubmeshIndex];
				if (submesh.VertexCount == 0) { continue; }

				cmd->SetVertexBinding(0, *mesh->Buffer, mesh->PositionOffset, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
				cmd->SetVertexBinding(1, *mesh->Buffer, mesh->NormalOffset, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
				cmd->SetVertexBinding(2, *mesh->Buffer, mesh->Texcoord0Offset, sizeof(glm::vec2), vk::VertexInputRate::eVertex);
				cmd->SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32);

				if (submesh.IndexCount > 0) {
					cmd->DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
				} else {
					cmd->Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
				}
			}
		}
	}
#endif

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
		_sceneBuffers.clear();
		_sceneImages.clear();

		Vulkan::ImageCreateInfo imageCI =
			Vulkan::ImageCreateInfo::RenderTarget(_imageSize.x, _imageSize.y, vk::Format::eB8G8R8A8Unorm);
		imageCI.Usage |= vk::ImageUsageFlagBits::eSampled;

		Vulkan::BufferCreateInfo bufferCI(
			Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer);

		const auto imageCount = _wsi.GetImageCount();
		for (int i = 0; i < imageCount; ++i) {
			_sceneBuffers.push_back(_wsi.GetDevice().CreateBuffer(bufferCI));
			_sceneImages.push_back(_wsi.GetDevice().CreateImage(imageCI));
		}
	}
}
