#pragma once

#include <Luna/Devices/Window.hpp>
#include <Luna/Threading/Threading.hpp>

namespace Luna {
namespace Vulkan {
class Context;
class Device;
}  // namespace Vulkan

class Graphics : public Module::Registrar<Graphics> {
	static inline const bool Registered = Register("Graphics", Stage::Render, Depends<Threading, Window>());

 public:
	Graphics();
	~Graphics() noexcept;

	virtual void Update() override;

 private:
	std::unique_ptr<Vulkan::Context> _context;
	std::unique_ptr<Vulkan::Device> _device;
};
}  // namespace Luna
