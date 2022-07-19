#pragma once

#include <memory>

#include "Vulkan/Common.hpp"

namespace Luna {
class Application {
 public:
	virtual ~Application() = default;

	static int Main(int argc, const char** argv);

 protected:
	virtual void Start()  = 0;
	virtual void Update() = 0;
	virtual void Stop()   = 0;

	std::shared_ptr<Vulkan::WSI> _wsi;

 private:
	void Initialize(std::shared_ptr<Vulkan::WSI> wsi);
	void Run();
};

extern std::unique_ptr<Application> CreateApplication(int argc, const char** argv);
}  // namespace Luna
