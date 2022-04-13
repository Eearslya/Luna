#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>

namespace Luna {
namespace Vulkan {
enum class DepthStencilUsage { None, ReadOnly, ReadWrite };
enum class DepthStencilOpBits {
	ClearDepthStencil    = 1 << 0,
	LoadDepthStencil     = 1 << 1,
	StoreDepthStencil    = 1 << 2,
	DepthStencilReadOnly = 1 << 3
};
using DepthStencilOps = Bitmask<DepthStencilOpBits>;

struct RenderPassInfo {
	struct SubpassInfo {
		std::array<uint32_t, MaxColorAttachments> ColorAttachments;
		uint32_t ColorAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> InputAttachments;
		uint32_t InputAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> ResolveAttachments;
		uint32_t ResolveAttachmentCount = 0;
		DepthStencilUsage DSUsage       = DepthStencilUsage::None;
	};

	std::array<const ImageView*, MaxColorAttachments> ColorAttachments;
	uint32_t ColorAttachmentCount                                      = 0;
	std::array<vk::ImageLayout, MaxColorAttachments> ColorFinalLayouts = {vk::ImageLayout::eUndefined};
	const ImageView* DepthStencilAttachment                            = nullptr;
	std::array<vk::ClearColorValue, MaxColorAttachments> ClearColors;
	vk::ClearDepthStencilValue ClearDepthStencil = {1.0f, 0};
	DepthStencilOps DSOps;

	vk::Rect2D RenderArea     = {{0, 0}, {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()}};
	uint32_t ClearAttachments = 0;
	uint32_t LoadAttachments  = 0;
	uint32_t StoreAttachments = 0;
	uint32_t BaseArrayLayer   = 0;
	uint32_t ArrayLayers      = 1;

	std::vector<SubpassInfo> Subpasses;
};

Hash HashRenderPassInfo(const RenderPassInfo& info, bool compatible = false);

class RenderPass : public HashedObject<RenderPass>, NonCopyable {
 public:
	RenderPass(Hash hash, Device& device, const RenderPassInfo& info);
	~RenderPass() noexcept;

	const vk::AttachmentReference& GetColorAttachment(uint32_t subpass, uint32_t attachment) const {
		return _subpasses[subpass].ColorAttachments[attachment];
	}
	uint32_t GetColorAttachmentCount(uint32_t subpass) const {
		return _subpasses[subpass].ColorAttachmentCount;
	}
	const vk::AttachmentReference& GetInputAttachment(uint32_t subpass, uint32_t attachment) const {
		return _subpasses[subpass].InputAttachments[attachment];
	}
	uint32_t GetInputAttachmentCount(uint32_t subpass) const {
		return _subpasses[subpass].InputAttachmentCount;
	}
	vk::RenderPass GetRenderPass() const {
		return _renderPass;
	}
	const RenderPassInfo& GetRenderPassInfo() const {
		return _renderPassInfo;
	}
	vk::SampleCountFlagBits GetSampleCount(uint32_t subpass) const {
		return _subpasses[subpass].Samples;
	}
	uint32_t GetSubpassCount() const {
		return static_cast<uint32_t>(_subpasses.size());
	}
	bool HasDepth(uint32_t subpass) const {
		return _subpasses[subpass].DepthStencilAttachment.attachment != VK_ATTACHMENT_UNUSED &&
		       FormatHasDepth(_depthStencilFormat);
	}
	bool HasStencil(uint32_t subpass) const {
		return _subpasses[subpass].DepthStencilAttachment.attachment != VK_ATTACHMENT_UNUSED &&
		       FormatHasStencil(_depthStencilFormat);
	}

 private:
	struct Subpass {
		std::array<vk::AttachmentReference, MaxColorAttachments> ColorAttachments = {VK_ATTACHMENT_UNUSED};
		uint32_t ColorAttachmentCount                                             = 0;
		std::array<vk::AttachmentReference, MaxColorAttachments> InputAttachments = {VK_ATTACHMENT_UNUSED};
		uint32_t InputAttachmentCount                                             = 0;
		vk::AttachmentReference DepthStencilAttachment                            = VK_ATTACHMENT_UNUSED;
		vk::SampleCountFlagBits Samples                                           = vk::SampleCountFlagBits::e1;
	};

	Device& _device;
	vk::RenderPass _renderPass;
	RenderPassInfo _renderPassInfo;
	std::array<vk::Format, MaxColorAttachments> _colorFormats;
	vk::Format _depthStencilFormat = vk::Format::eUndefined;
	std::vector<Subpass> _subpasses;
};

class Framebuffer : public Cookie, public InternalSyncEnabled, NonCopyable {
 public:
	Framebuffer(Device& device, const RenderPass& renderPass, const RenderPassInfo& renderPassInfo);
	~Framebuffer() noexcept;

	const vk::Extent2D& GetExtent() const {
		return _extent;
	}
	vk::Framebuffer GetFramebuffer() const {
		return _framebuffer;
	}
	const RenderPass& GetCompatibleRenderPass() const {
		return _renderPass;
	}

 private:
	Device& _device;
	vk::Framebuffer _framebuffer;
	const RenderPass& _renderPass;
	vk::Extent2D _extent;
};

class FramebufferAllocator {
	constexpr static const int FramebufferRingSize = 8;

 public:
	explicit FramebufferAllocator(Device& device);

	void BeginFrame();
	void Clear();
	Framebuffer& RequestFramebuffer(const RenderPassInfo& info);

 private:
	struct FramebufferNode : TemporaryHashMapEnabled<FramebufferNode>,
													 IntrusiveListEnabled<FramebufferNode>,
													 public Framebuffer {
		FramebufferNode(Device& device, const RenderPass& renderPass, const RenderPassInfo& renderPassInfo);
	};

	Device& _device;
	TemporaryHashMap<FramebufferNode, FramebufferRingSize, false> _framebuffers;
#ifdef LUNA_VULKAN_MT
	std::mutex _mutex;
#endif
};

class TransientAttachmentAllocator {
	constexpr static const int TransientAttachmentRingSize = 8;

 public:
	explicit TransientAttachmentAllocator(Device& device);

	void BeginFrame();
	void Clear();
	ImageHandle RequestAttachment(const vk::Extent2D& extent,
	                              vk::Format format,
	                              uint32_t index                  = 0,
	                              vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
	                              uint32_t layers                 = 1);

 private:
	struct TransientNode : TemporaryHashMapEnabled<TransientNode>, IntrusiveListEnabled<TransientNode> {
		explicit TransientNode(ImageHandle image);

		ImageHandle Image;
	};

	Device& _device;
	TemporaryHashMap<TransientNode, TransientAttachmentRingSize, false> _attachments;
#ifdef LUNA_VULKAN_MT
	std::mutex _mutex;
#endif
};
}  // namespace Vulkan

template <>
struct EnableBitmaskOperators<Vulkan::DepthStencilOpBits> : std::true_type {};
}  // namespace Luna

template <>
struct std::hash<Luna::Vulkan::RenderPassInfo> {
	size_t operator()(const Luna::Vulkan::RenderPassInfo& info) {
		return static_cast<size_t>(Luna::Vulkan::HashRenderPassInfo(info, false));
	}
};
