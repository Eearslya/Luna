#include "Cookie.hpp"

#include "Device.hpp"

namespace Luna {
namespace Vulkan {
Cookie::Cookie(Device& device) : _cookie(device.AllocateCookie()) {}
}  // namespace Vulkan
}  // namespace Luna
