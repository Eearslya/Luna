#include <iostream>

#include "Utility/Log.hpp"
#include "Vulkan/Context.hpp"
#include "Vulkan/Device.hpp"

using namespace Luna;

int main(int argc, const char** argv) {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	try {
		Vulkan::Context context;
		Vulkan::Device device(context);
	} catch (const std::exception& e) {
		std::cerr << "Fatal uncaught exception when initializing Vulkan:\n\t" << e.what() << std::endl;
		return 1;
	}

	Log::Shutdown();

	return 0;
}
