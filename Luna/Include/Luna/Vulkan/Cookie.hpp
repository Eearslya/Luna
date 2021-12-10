#pragma once

#include <cstdint>

namespace Luna {
namespace Vulkan {
class Device;

class Cookie {
 public:
	Cookie(Device& device);
	virtual ~Cookie() = default;

	uint64_t GetCookie() const {
		return _cookie;
	}

 private:
	uint64_t _cookie;
};

class InternalSyncEnabled {
	friend class Device;

 protected:
	bool _internalSync = false;
};
}  // namespace Vulkan
}  // namespace Luna
