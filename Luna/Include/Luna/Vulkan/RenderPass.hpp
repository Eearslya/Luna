#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>

namespace Luna {
namespace Vulkan {
struct RenderPassInfo {
	enum class DepthStencilUsage { None, ReadOnly, ReadWrite };

	struct Subpass {
		uint32_t ColorAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> ColorAttachments;

		uint32_t InputAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> InputAttachments;

		uint32_t ResolveAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> ResolveAttachments;

		DepthStencilUsage DepthStencil = DepthStencilUsage::ReadWrite;
	};

	uint32_t ColorAttachmentCount = 0;
	std::array<const ImageView*, MaxColorAttachments> ColorAttachments;
	std::array<vk::ClearColorValue, MaxColorAttachments> ColorClearValues;

	const ImageView* DepthStencilAttachment           = nullptr;
	vk::ClearDepthStencilValue DepthStencilClearValue = vk::ClearDepthStencilValue(1.0f, 0);

	RenderPassOpFlags Flags      = {};
	uint32_t ClearAttachmentMask = 0;
	uint32_t LoadAttachmentMask  = 0;
	uint32_t StoreAttachmentMask = 0;

	uint32_t BaseLayer   = 0;
	uint32_t ArrayLayers = 0;

	vk::Rect2D RenderArea =
		vk::Rect2D({0, 0}, {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()});
	std::vector<Subpass> Subpasses;
};

class RenderPass : public HashedObject<RenderPass> {
 public:
	RenderPass(Hash hash, Device& device, const RenderPassInfo& rpInfo);
	RenderPass(Hash hash, Device& device, const vk::RenderPassCreateInfo2& rpCI);
	RenderPass(const RenderPass&)            = delete;
	RenderPass(RenderPass&&)                 = delete;
	RenderPass& operator=(const RenderPass&) = delete;
	RenderPass& operator=(RenderPass&&)      = delete;
	~RenderPass() noexcept;

	uint32_t GetColorAttachmentCount(uint32_t subpass) const {
		return _subpasses[subpass].ColorAttachmentCount;
	}
	const vk::AttachmentReference2& GetColorAttachment(uint32_t subpass, uint32_t att) const {
		return _subpasses[subpass].ColorAttachments[att];
	}
	uint32_t GetInputAttachmentCount(uint32_t subpass) const {
		return _subpasses[subpass].InputAttachmentCount;
	}
	const vk::AttachmentReference2& GetInputAttachment(uint32_t subpass, uint32_t att) const {
		return _subpasses[subpass].InputAttachments[att];
	}
	vk::RenderPass GetRenderPass() const {
		return _renderPass;
	}
	vk::SampleCountFlagBits GetSampleCount(uint32_t subpass) const {
		return _subpasses[subpass].SampleCount;
	}
	size_t GetSubpassCount() const {
		return _subpasses.size();
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
	struct SubpassInfo {
		uint32_t ColorAttachmentCount = 0;
		std::array<vk::AttachmentReference2, MaxColorAttachments> ColorAttachments;
		uint32_t InputAttachmentCount = 0;
		std::array<vk::AttachmentReference2, MaxColorAttachments> InputAttachments;
		vk::AttachmentReference2 DepthStencilAttachment;
		vk::SampleCountFlagBits SampleCount = {};
	};

	void SetupSubpasses(const vk::RenderPassCreateInfo2& rpCI);

	Device& _device;
	vk::RenderPass _renderPass;

	std::array<vk::Format, MaxColorAttachments> _colorAttachmentFormats;
	vk::Format _depthStencilFormat = vk::Format::eUndefined;
	std::vector<SubpassInfo> _subpasses;
};

class Framebuffer : public Cookie, public InternalSyncEnabled {
 public:
	Framebuffer(Device& device, const RenderPass& renderPass, const RenderPassInfo& rpInfo);
	~Framebuffer() noexcept;

	const RenderPass& GetCompatibleRenderPass() const {
		return _renderPass;
	}
	const vk::Extent2D& GetExtent() const {
		return _extent;
	}
	vk::Framebuffer GetFramebuffer() const {
		return _framebuffer;
	}

 private:
	Device& _device;
	vk::Framebuffer _framebuffer;
	const RenderPass& _renderPass;
	RenderPassInfo _renderPassInfo;
	vk::Extent2D _extent = {0, 0};
};

class FramebufferAllocator {
 public:
	FramebufferAllocator(Device& device);

	void BeginFrame();
	void Clear();
	Framebuffer& RequestFramebuffer(const RenderPassInfo& rpInfo);

 private:
	struct FramebufferNode : TemporaryHashMapEnabled<FramebufferNode>,
													 IntrusiveListEnabled<FramebufferNode>,
													 Framebuffer {
		FramebufferNode(Device& device, const RenderPass& renderPass, const RenderPassInfo& rpInfo);
	};

	Device& _device;
	TemporaryHashMap<FramebufferNode, 8, false> _framebuffers;
	std::mutex _lock;
};

class TransientAttachmentAllocator {
	constexpr static int TransientAttachmentRingSize = 8;

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
	std::mutex _mutex;
};
}  // namespace Vulkan
}  // namespace Luna
