#pragma once

#include <Luna/Utility/EnumClass.hpp>

namespace Luna {
enum class AttachmentInfoFlagBits {
	Persistent        = 1 << 0,
	UnormSrgbAlias    = 1 << 1,
	SupportsPrerotate = 1 << 2,
	GenerateMips      = 1 << 3,
	InternalTransient = 1 << 16,
	InternalProxy     = 1 << 17,
};
using AttachmentInfoFlags = Bitmask<AttachmentInfoFlagBits>;

enum class RendererFlushFlagBits {
	FrontFaceClockwise   = 1 << 0,
	DepthBias            = 1 << 1,
	DepthStencilReadOnly = 1 << 2,
	NoColor              = 1 << 3,
	Backface             = 1 << 4,
	SkipSorting          = 1 << 5,
	DepthTestInvert      = 1 << 6,
	DepthTestEqual       = 1 << 7,
};
using RendererFlushFlags = Bitmask<RendererFlushFlagBits>;

enum class RendererOptionFlagBits { EnableShadows = 1 << 0 };
using RendererOptionFlags = Bitmask<RendererOptionFlagBits>;

enum class RenderGraphQueueFlagBits {
	Graphics      = 1 << 0,
	Compute       = 1 << 1,
	AsyncCompute  = 1 << 2,
	AsyncGraphics = 1 << 3,
};
using RenderGraphQueueFlags = Bitmask<RenderGraphQueueFlagBits>;

enum class RenderableType { Mesh };
constexpr static const uint32_t RenderableTypeCount = 1;

/**
 * Identifies what responsibilities a given Renderer object is made to handle.
 */
enum class RendererSuiteType { ForwardOpaque, ForwardTransparent, PrepassDepth, Deferred };
constexpr static const uint32_t RendererSuiteTypeCount = 4;

enum class RendererType { GeneralForward, GeneralDeferred, DepthOnly, Flat };
constexpr static const uint32_t RendererTypeCount = 4;

enum class RenderQueueType { Opaque, OpaqueEmissive, Light, Transparent };
constexpr static const uint32_t RenderQueueTypeCount = 4;

/**
 * Used to describe the size of an image attachment.
 */
enum class SizeClass {
	Absolute,          /** Size is taken as absolute pixel size. */
	SwapchainRelative, /** Size is taken as a multiplier to swapchain size, such that 1.0 is equal to swapchain size. */
	InputRelative      /** Size is taken as a multiplier to another named image attachment's size. */
};
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::AttachmentInfoFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::RendererFlushFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::RendererOptionFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::RenderGraphQueueFlagBits> : std::true_type {};
