#pragma once

#include <glm/glm.hpp>

#include "Vulkan/Common.hpp"

namespace Luna {
class Scene;
}

class SceneRenderer {
 public:
	SceneRenderer(Luna::Vulkan::WSI& wsi);
	~SceneRenderer() noexcept;

	Luna::Vulkan::ImageHandle& GetImage(uint32_t frameIndex);
	void Render(Luna::Vulkan::CommandBufferHandle& cmd, Luna::Scene& scene, uint32_t frameIndex);
	void SetImageSize(const glm::uvec2& size);

 private:
	struct SceneData {
		glm::mat4 Projection;
		glm::mat4 View;
	};

	struct PushConstant {
		glm::mat4 Model;
	};

	Luna::Vulkan::WSI& _wsi;
	Luna::Vulkan::Program* _program = nullptr;
	glm::uvec2 _imageSize;
	std::vector<Luna::Vulkan::BufferHandle> _sceneBuffers;
	std::vector<Luna::Vulkan::ImageHandle> _sceneImages;
};
