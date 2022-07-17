#pragma once

#include "Camera.hpp"

namespace Luna {
struct CameraComponent {
	CameraComponent()                       = default;
	CameraComponent(const CameraComponent&) = default;

	Luna::Camera Camera;
	bool Primary = true;
};
}  // namespace Luna
