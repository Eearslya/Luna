#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <memory>
#include <vector>

namespace Luna {
namespace Vulkan {
class Context;

class WSIPlatform {
 public:
	virtual ~WSIPlatform() noexcept = default;

	virtual std::vector<const char*> GetRequiredDeviceExtensions() const   = 0;
	virtual std::vector<const char*> GetRequiredInstanceExtensions() const = 0;
	virtual bool IsAlive() const                                           = 0;

	virtual void Update() = 0;

 protected:
 private:
};

class WSI {
 public:
	WSI(WSIPlatform* platform);
	~WSI() noexcept;

	void BeginFrame();
	void EndFrame();
	bool IsAlive();
	void Update();

 private:
	std::unique_ptr<WSIPlatform> _platform;
	IntrusivePtr<Context> _context;
};
}  // namespace Vulkan
}  // namespace Luna
