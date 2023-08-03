#pragma once

namespace Luna {
namespace Vulkan {
class InternalSyncEnabled {
 public:
	void SetInternalSync() {
		_internalSync = true;
	}

 protected:
	bool _internalSync = false;
};
}  // namespace Vulkan
}  // namespace Luna
