#include <iostream>

#include "Utility/Log.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Context.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"

using namespace Luna;

int main(int argc, const char** argv) {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	try {
		Vulkan::Context context;
		Vulkan::Device device(context);

		const uint8_t bufferData[64] = {0};
		const Vulkan::BufferCreateInfo bufferCI(Vulkan::BufferDomain::Device, 64, vk::BufferUsageFlagBits::eStorageBuffer);
		auto buffer = device.CreateBuffer(bufferCI, bufferData);

		const uint32_t imageData[] = {0u, ~0u, 0u, ~0u, ~0u, 0u, ~0u, 0u, 0u, ~0u, 0u, ~0u, ~0u, 0u, ~0u, 0u};
		const Vulkan::ImageCreateInfo imageCI =
			Vulkan::ImageCreateInfo::Immutable2D(4, 4, vk::Format::eR8G8B8A8Unorm, true);
		const Vulkan::ImageInitialData initialImage = {.Data = imageData};
		auto image                                  = device.CreateImage(imageCI, &initialImage);
		auto& view                                  = image->GetView();
	} catch (const std::exception& e) {
		std::cerr << "Fatal uncaught exception when initializing Vulkan:\n\t" << e.what() << std::endl;
		return 1;
	}

	Log::Shutdown();

	return 0;
}
