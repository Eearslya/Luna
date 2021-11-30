#include <Luna/Vulkan/Cookie.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
Cookie::Cookie(Device& device) : _cookie(device.AllocateCookie({})) {}
}  // namespace Vulkan
}  // namespace Luna
