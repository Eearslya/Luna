#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
struct Environment;

struct EnvironmentDeleter {
	void operator()(Environment* environment);
};

struct Environment : public IntrusivePtrEnabled<Environment, EnvironmentDeleter, MultiThreadCounter> {
	Environment();
	~Environment() noexcept;

	Vulkan::ImageHandle Skybox;
	Vulkan::ImageHandle Irradiance;
	Vulkan::ImageHandle Prefiltered;
	Vulkan::ImageHandle BrdfLut;
	std::atomic_bool Ready = false;
};

using EnvironmentHandle = IntrusivePtr<Environment>;
}  // namespace Luna
