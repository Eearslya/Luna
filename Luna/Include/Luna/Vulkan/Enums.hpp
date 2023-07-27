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

#define DefineVulkanEnum(Name, ValueCount, ...)        \
	enum class Name { __VA_ARGS__ };                     \
	constexpr static const int Name##Count = ValueCount; \
	template <>                                          \
	struct VulkanEnumEnabled<Name> : std::true_type {};  \
	template <>                                          \
	const char* VulkanEnumToString<Name>(const Name value)

DefineVulkanEnum(QueueType, 3, Graphics, Transfer, Compute) {
	switch (value) {
		case QueueType::Graphics:
			return "Graphics";
		case QueueType::Transfer:
			return "Transfer";
		case QueueType::Compute:
			return "Compute";
	}

	return "Unknown";
}

#undef DefineVulkanEnum
}  // namespace Vulkan
}  // namespace Luna

template <Luna::Vulkan::IsVulkanEnum T>
struct fmt::formatter<T> : fmt::formatter<std::string> {
	auto format(const T value, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(), "{}", Luna::Vulkan::VulkanEnumToString(value));
	}
};
