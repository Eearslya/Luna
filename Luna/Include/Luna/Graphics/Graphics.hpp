#pragma once

#include <Luna/Devices/Window.hpp>

namespace Luna {
namespace Vulkan {
class Context;
}

class Graphics : public Module::Registrar<Graphics> {
	static inline const bool Registered = Register("Graphics", Stage::Render, Depends<Window>());

 public:
	Graphics();
	~Graphics() noexcept;

	virtual void Update() override;

 private:
	std::unique_ptr<Vulkan::Context> _context;
};
}  // namespace Luna
