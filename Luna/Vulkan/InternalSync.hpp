#pragma once

namespace Luna {
namespace Vulkan {
class InternalSync {
 public:
	void SetInternalSync() {
		_internalSync = true;
	}

 protected:
	bool _internalSync = false;
};
}  // namespace Vulkan
}  // namespace Luna
