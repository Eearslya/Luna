#include <Luna/Application/Application.hpp>
#include <Luna/Application/GlfwPlatform.hpp>
#include <memory>

int main(int argc, const char** argv) {
	std::unique_ptr<Luna::Application> app(Luna::CreateApplication(argc, argv));
	if (!app) { return 1; }

	std::unique_ptr<Luna::Vulkan::WSIPlatform> platform =
		std::make_unique<Luna::GlfwPlatform>(app->GetName(), app->GetDefaultSize());
	if (!app->InitializeWSI(platform.release())) { return 2; }

	int returnCode = app->Run();

	app.reset();

	return returnCode;
}
