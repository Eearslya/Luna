#pragma once

#include <Luna/Utility/TemporaryHashMap.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>

namespace Luna {
namespace Vulkan {
struct RenderPassInfo {
	struct Subpass {
		uint32_t ColorAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> ColorAttachments;
		uint32_t InputAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> InputAttachments;
		uint32_t ResolveAttachmentCount = 0;
		std::array<uint32_t, MaxColorAttachments> ResolveAttachments;
		DepthStencilUsage DepthStencil = DepthStencilUsage::ReadWrite;
	};

	RenderPassFlags Flags = {};

	uint32_t ColorAttachmentCount = 0;
	std::array<const ImageView*, MaxColorAttachments> ColorAttachments;
	std::array<vk::ClearColorValue, MaxColorAttachments> ClearColors;
	const ImageView* DepthStencilAttachment      = nullptr;
	vk::ClearDepthStencilValue ClearDepthStencil = vk::ClearDepthStencilValue(1.0f, 0);

	uint32_t ClearAttachmentMask = 0;
	uint32_t LoadAttachmentMask  = 0;
	uint32_t StoreAttachmentMask = 0;

	uint32_t BaseLayer   = 0;
	uint32_t ArrayLayers = 1;

	vk::Rect2D RenderArea = vk::Rect2D(
		vk::Offset2D(0, 0), vk::Extent2D(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()));

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

	[[nodiscard]] uint32_t GetColorAttachmentCount(uint32_t subpass) const noexcept {
		return _subpasses[subpass].ColorAttachmentCount;
	}
	[[nodiscard]] const vk::AttachmentReference2& GetColorAttachment(uint32_t subpass, uint32_t att) const noexcept {
		return _subpasses[subpass].ColorAttachments[att];
	}
	[[nodiscard]] uint32_t GetInputAttachmentCount(uint32_t subpass) const noexcept {
		return _subpasses[subpass].InputAttachmentCount;
	}
	[[nodiscard]] const vk::AttachmentReference2& GetInputAttachment(uint32_t subpass, uint32_t att) const noexcept {
		return _subpasses[subpass].InputAttachments[att];
	}
	[[nodiscard]] vk::RenderPass GetRenderPass() const noexcept {
		return _renderPass;
	}
	[[nodiscard]] vk::SampleCountFlagBits GetSampleCount(uint32_t subpass) const noexcept {
		return _subpasses[subpass].SampleCount;
	}
	[[nodiscard]] size_t GetSubpassCount() const noexcept {
		return _subpasses.size();
	}
	[[nodiscard]] bool HasDepth(uint32_t subpass) const noexcept {
		return _subpasses[subpass].DepthStencilAttachment.attachment != VK_ATTACHMENT_UNUSED &&
		       FormatHasDepth(_depthStencilFormat);
	}
	[[nodiscard]] bool HasStencil(uint32_t subpass) const noexcept {
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

struct FramebufferNode : TemporaryHashMapEnabled<FramebufferNode>, IntrusiveListEnabled<FramebufferNode>, Framebuffer {
	FramebufferNode(Device& device, const RenderPass& renderPass, const RenderPassInfo& rpInfo);
};

struct TransientAttachmentNode : TemporaryHashMapEnabled<TransientAttachmentNode>,
																 IntrusiveListEnabled<TransientAttachmentNode> {
	TransientAttachmentNode(ImageHandle image);

	ImageHandle Image;
};
}  // namespace Vulkan
}  // namespace Luna
