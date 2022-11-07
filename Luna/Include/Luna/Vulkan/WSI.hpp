#pragma once

#include <memory>

namespace Luna {
namespace Vulkan {
class WSIPlatform {
 public:
	virtual ~WSIPlatform() noexcept = default;

	virtual bool IsAlive() const = 0;

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
};
}  // namespace Vulkan
}  // namespace Luna
