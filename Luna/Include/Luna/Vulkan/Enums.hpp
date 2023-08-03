#pragma once

#include <Luna/Utility/Bitmask.hpp>

namespace Luna {
namespace Vulkan {
template <typename T>
struct VulkanEnumEnabled : public std::false_type {};

template <typename T>
concept IsVulkanEnum = VulkanEnumEnabled<T>::value;

template <typename T>
static const char* VulkanEnumToString(const T value) {
	static_assert(IsVulkanEnum<T>, "VulkanEnumToString can only be used on Luna::Vulkan enums");
}

#define BeginVulkanEnum(Name, ValueCount, ...)             \
	enum class Name { __VA_ARGS__ };                         \
	constexpr static const int Name##Count = ValueCount;     \
	template <>                                              \
	struct VulkanEnumEnabled<Name> : std::true_type {};      \
	template <>                                              \
	const char* VulkanEnumToString<Name>(const Name value) { \
		switch (value) {
#define EnumCase(Name, Value) \
	case Name::Value:           \
		return #Value;
#define EndVulkanEnum() \
	}                     \
	return "Unknown";     \
	}

BeginVulkanEnum(QueueType, 3, Graphics, Transfer, Compute);
EnumCase(QueueType, Graphics);
EnumCase(QueueType, Transfer);
EnumCase(QueueType, Compute);
EndVulkanEnum();

BeginVulkanEnum(CommandBufferType,
                4,
                Generic       = int(QueueType::Graphics),
                AsyncCompute  = int(QueueType::Compute),
                AsyncTransfer = int(QueueType::Transfer),
                AsyncGraphics = int(QueueTypeCount));
EnumCase(CommandBufferType, Generic);
EnumCase(CommandBufferType, AsyncCompute);
EnumCase(CommandBufferType, AsyncTransfer);
EnumCase(CommandBufferType, AsyncGraphics);
EndVulkanEnum();

#undef EndVulkanEnum
#undef EnumCase
#undef BeginVulkanEnum
}  // namespace Vulkan
}  // namespace Luna

template <Luna::Vulkan::IsVulkanEnum T>
struct fmt::formatter<T> : fmt::formatter<std::string> {
	auto format(const T value, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(), "{}", Luna::Vulkan::VulkanEnumToString(value));
	}
};