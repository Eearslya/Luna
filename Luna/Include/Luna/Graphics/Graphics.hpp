#pragma once

#include <Luna/Devices/Window.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Threading/Threading.hpp>
#include <glm/glm.hpp>

namespace Luna {
namespace Vulkan {
class Context;
class Device;
}  // namespace Vulkan

class Graphics : public Module::Registrar<Graphics> {
	static inline const bool Registered = Register("Graphics", Stage::Render, Depends<Filesystem, Threading, Window>());

 public:
	Graphics();
	~Graphics() noexcept;

	virtual void Update() override;

 private:
	bool BeginFrame();
	void EndFrame();

	struct SceneData {
		glm::mat4 Projection;
		glm::mat4 View;
	};
	struct SubMesh {
		uint32_t FirstVertex = 0;
		uint32_t FirstIndex  = 0;
		uint32_t IndexCount  = 0;
		uint32_t Material    = 0;
	};
	struct MaterialData {
		float AlphaCutoff = 0.0f;
	};
	struct Material {
		Vulkan::ImageHandle Albedo;
		Vulkan::BufferHandle Data;
	};
	struct StaticMesh {
		std::vector<Material> Materials;
		std::vector<SubMesh> SubMeshes;
	};

	std::unique_ptr<Vulkan::Context> _context;
	std::unique_ptr<Vulkan::Device> _device;
	std::unique_ptr<Vulkan::Swapchain> _swapchain;

	Vulkan::Program* _program = nullptr;
	StaticMesh _mesh;
	Vulkan::BufferHandle _positionBuffer;
	Vulkan::BufferHandle _normalBuffer;
	Vulkan::BufferHandle _indexBuffer;
	Vulkan::BufferHandle _texcoordBuffer;
	Vulkan::BufferHandle _sceneBuffer;
	Vulkan::ImageHandle _texture;
	uint64_t _indexCount = 0;
};
}  // namespace Luna
