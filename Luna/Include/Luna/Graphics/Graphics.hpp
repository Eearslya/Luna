#pragma once

#include <Luna/Devices/Window.hpp>

namespace Luna {
class Graphics : public Module::Registrar<Graphics> {
	static inline const bool Registered = Register("Graphics", Stage::Render, Depends<Window>());

 public:
	Graphics();
	~Graphics() noexcept;

	virtual void Update() override;

 private:
};
}  // namespace Luna
