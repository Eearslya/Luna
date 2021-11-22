#pragma once

#include <Luna/Devices/Keyboard.hpp>
#include <Luna/Devices/Mouse.hpp>

namespace Luna {
class Keyboard;
class Mouse;

class Input : public Module::Registrar<Input> {
	static inline const bool Registered = Register("Input", Stage::Normal, Depends<Keyboard, Mouse>());

 public:
	Input();

	virtual void Update() override;
};
}  // namespace Luna
