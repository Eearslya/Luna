#include <iostream>

#include "Utility/Log.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Context.hpp"
#include "Vulkan/Device.hpp"

using namespace Luna;

int main(int argc, const char** argv) {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	try {
		Vulkan::Context context;
		Vulkan::Device device(context);

		const Vulkan::BufferCreateInfo bufferCI(Vulkan::BufferDomain::Device, 64, vk::BufferUsageFlagBits::eStorageBuffer);
		auto buffer = device.CreateBuffer(bufferCI);
	} catch (const std::exception& e) {
		std::cerr << "Fatal uncaught exception when initializing Vulkan:\n\t" << e.what() << std::endl;
		return 1;
	}

	Log::Shutdown();

	return 0;
}
