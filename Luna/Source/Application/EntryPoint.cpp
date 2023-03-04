#include <Luna/Application/Application.hpp>
#include <Luna/Application/GlfwPlatform.hpp>
#include <memory>

int main(int argc, const char** argv) {
	std::unique_ptr<Luna::Vulkan::WSIPlatform> platform = std::make_unique<Luna::GlfwPlatform>();
	std::unique_ptr<Luna::Application> application(Luna::CreateApplication(argc, argv));
	application->InitializeWSI(platform.get());
	return application->Run();
}
