#pragma once

#include <Luna/Common.hpp>
#include <Luna/Utility/Bitmask.hpp>

namespace Luna {
enum class AttachmentInfoFlagBits {
	Persistent        = 1 << 0,
	UnormSrgbAlias    = 1 << 1,
	SupportsPrerotate = 1 << 2,
	GenerateMips      = 1 << 3,
	CreateMipViews    = 1 << 4,
	InternalTransient = 1 << 16,
	InternalProxy     = 1 << 17,
};
using AttachmentInfoFlags = Bitmask<AttachmentInfoFlagBits>;

enum class RenderGraphQueueFlagBits {
	Graphics      = 1 << 0,
	Compute       = 1 << 1,
	AsyncCompute  = 1 << 2,
	AsyncGraphics = 1 << 3
};
using RenderGraphQueueFlags = Bitmask<RenderGraphQueueFlagBits>;

/**
 * Used to describe the size of an image attachment.
 */
enum class SizeClass {
	Absolute,          /** Size is taken as an absolute pixel size. */
	SwapchainRelative, /** Size is taken as a multiplier to swapchain size, such that 1.0 is equal to swapchain size. */
	InputRelative      /** Size is taken as a multiplier to another named image attachment's size. */
};
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::AttachmentInfoFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::RenderGraphQueueFlagBits> : std::true_type {};
