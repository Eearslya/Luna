#pragma once

#include <Luna/Common.hpp>

namespace Luna {
namespace Vulkan {
class Device;

class Cookie {
 public:
	Cookie(Device& device);

	[[nodiscard]] uint64_t GetCookie() const noexcept {
		return _cookie;
	}

 private:
	const uint64_t _cookie;
};
}  // namespace Vulkan
}  // namespace Luna
