#pragma once

#include <Luna/Devices/Window.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Threading/Threading.hpp>

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

	std::unique_ptr<Vulkan::Context> _context;
	std::unique_ptr<Vulkan::Device> _device;
	std::unique_ptr<Vulkan::Swapchain> _swapchain;

	Vulkan::Program* _program = nullptr;
	Vulkan::BufferHandle _colorBuffer;
	Vulkan::BufferHandle _indexBuffer;
	Vulkan::BufferHandle _vertexBuffer;
};
}  // namespace Luna
