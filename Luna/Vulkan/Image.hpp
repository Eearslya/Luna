#pragma once

#include <vk_mem_alloc.h>

#include "Common.hpp"
#include "Cookie.hpp"
#include "Format.hpp"
#include "InternalSync.hpp"
#include "Utility/EnumClass.hpp"

namespace Luna {
namespace Vulkan {
inline vk::AccessFlags ImageLayoutToAccess(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eInputAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentRead;
		case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;
		case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;
		default:
			return vk::AccessFlags(~0u);
	}
}

inline vk::AccessFlags ImageUsageToAccess(vk::ImageUsageFlags usage) {
	vk::AccessFlags access = {};

	if (usage & (vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)) {
		access |= vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eSampled) { access |= vk::AccessFlagBits::eShaderRead; }
	if (usage & vk::ImageUsageFlagBits::eStorage) {
		access |= vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eColorAttachment) {
		access |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
		access |= vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eInputAttachment) { access |= vk::AccessFlagBits::eInputAttachmentRead; }
	if (usage & vk::ImageUsageFlagBits::eTransientAttachment) {
		access &= vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead |
		          vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite |
		          vk::AccessFlagBits::eInputAttachmentRead;
	}

	return access;
}

inline vk::PipelineStageFlags ImageUsageToStages(vk::ImageUsageFlags usage) {
	vk::PipelineStageFlags stages = {};

	if (usage & (vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)) {
		stages |= vk::PipelineStageFlagBits::eTransfer;
	}
	if (usage & vk::ImageUsageFlagBits::eSampled) {
		stages |= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eVertexShader |
		          vk::PipelineStageFlagBits::eFragmentShader;
	}
	if (usage & vk::ImageUsageFlagBits::eStorage) { stages |= vk::PipelineStageFlagBits::eComputeShader; }
	if (usage & vk::ImageUsageFlagBits::eColorAttachment) { stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput; }
	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
		stages |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
	}
	if (usage & vk::ImageUsageFlagBits::eInputAttachment) { stages |= vk::PipelineStageFlagBits::eFragmentShader; }
	if (usage & vk::ImageUsageFlagBits::eTransientAttachment) {
		vk::PipelineStageFlags possible = vk::PipelineStageFlagBits::eColorAttachmentOutput |
		                                  vk::PipelineStageFlagBits::eEarlyFragmentTests |
		                                  vk::PipelineStageFlagBits::eLateFragmentTests;
		if (usage & vk::ImageUsageFlagBits::eInputAttachment) { possible |= vk::PipelineStageFlagBits::eFragmentShader; }

		stages &= possible;
	}

	return stages;
}

enum class ImageDomain { Physical, Transient };

enum class ImageCreateFlagBits {
	GenerateMipmaps              = 1 << 0,
	ForceArray                   = 1 << 1,
	MutableSrgb                  = 1 << 2,
	ConcurrentQueueGraphics      = 1 << 3,
	ConcurrentQueueAsyncCompute  = 1 << 4,
	ConcurrentQueueAsyncGraphics = 1 << 5,
	ConcurrentQueueAsyncTransfer = 1 << 6,
};
using ImageCreateFlags = Bitmask<ImageCreateFlagBits>;

struct ImageCreateInfo {
	ImageDomain Domain              = ImageDomain::Physical;
	vk::Format Format               = vk::Format::eUndefined;
	vk::ImageLayout InitialLayout   = vk::ImageLayout::eGeneral;
	vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
	vk::ImageType Type              = vk::ImageType::e2D;
	vk::ImageUsageFlags Usage       = {};

	uint32_t Width  = 0;
	uint32_t Height = 0;
	uint32_t Depth  = 1;

	uint32_t ArrayLayers = 1;
	uint32_t MipLevels   = 1;

	vk::ImageCreateFlags Flags = {};
	ImageCreateFlags MiscFlags = {};

	vk::ImageViewType GetImageViewType() const;

	static ImageCreateInfo Immutable2D(uint32_t width, uint32_t height, vk::Format format, bool mipmaps = false) {
		return ImageCreateInfo{.Format        = format,
		                       .InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		                       .Type          = vk::ImageType::e2D,
		                       .Usage         = vk::ImageUsageFlagBits::eSampled,
		                       .Width         = width,
		                       .Height        = height,
		                       .Depth         = 1,
		                       .MipLevels     = mipmaps ? 0u : 1u,
		                       .MiscFlags     = mipmaps ? ImageCreateFlagBits::GenerateMipmaps : ImageCreateFlags{}};
	}

	static ImageCreateInfo RenderTarget(uint32_t width, uint32_t height, vk::Format format) {
		return {.Domain        = ImageDomain::Physical,
		        .Format        = format,
		        .InitialLayout = FormatHasDepthOrStencil(format) ? vk::ImageLayout::eDepthStencilAttachmentOptimal
		                                                         : vk::ImageLayout::eColorAttachmentOptimal,
		        .Samples       = vk::SampleCountFlagBits::e1,
		        .Type          = vk::ImageType::e2D,
		        .Usage         = (FormatHasDepthOrStencil(format) ? vk::ImageUsageFlagBits::eDepthStencilAttachment
		                                                          : vk::ImageUsageFlagBits::eColorAttachment),
		        .Width         = width,
		        .Height        = height,
		        .Depth         = 1,
		        .ArrayLayers   = 1,
		        .MipLevels     = 1,
		        .Flags         = {}};
	}

	static ImageCreateInfo TransientRenderTarget(uint32_t width, uint32_t height, vk::Format format) {
		return {.Domain = ImageDomain::Transient,
		        .Format = format,
		        .Type   = vk::ImageType::e2D,
		        .Usage  = (FormatHasDepthOrStencil(format) ? vk::ImageUsageFlagBits::eDepthStencilAttachment
		                                                   : vk::ImageUsageFlagBits::eColorAttachment) |
		                 vk::ImageUsageFlagBits::eInputAttachment,
		        .Width       = width,
		        .Height      = height,
		        .Depth       = 1,
		        .ArrayLayers = 1,
		        .MipLevels   = 1};
	}
};

struct ImageViewCreateInfo {
	const Image* Image      = nullptr;
	vk::Format Format       = vk::Format::eUndefined;
	uint32_t BaseMipLevel   = 0;
	uint32_t MipLevels      = VK_REMAINING_MIP_LEVELS;
	uint32_t BaseArrayLayer = 0;
	uint32_t ArrayLayers    = VK_REMAINING_ARRAY_LAYERS;
	vk::ImageViewType Type  = {};
};

struct ImageViewDeleter {
	void operator()(ImageView* view);
};

class ImageView : public IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>,
									public Cookie,
									public InternalSync {
 public:
	friend struct ImageViewDeleter;

	ImageView(Device& device, vk::ImageView view, const ImageViewCreateInfo& viewCI);
	~ImageView() noexcept;

	const ImageViewCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::ImageView GetFloatView() const {
		return _depthView ? _depthView : _view;
	}
	const Image& GetImage() const {
		return *_createInfo.Image;
	}
	vk::ImageView GetImageView() const {
		return _view;
	}
	vk::ImageView GetIntegerView() const {
		return _stencilView ? _stencilView : _view;
	}

	vk::ImageView GetRenderTargetView(uint32_t layer) const;

	void SetAltViews(vk::ImageView depth, vk::ImageView stencil) {
		_depthView   = depth;
		_stencilView = stencil;
	}
	void SetRenderTargetViews(const std::vector<vk::ImageView>& views) {
		_renderTargetViews = views;
	}
	void SetSrgbView(vk::ImageView view) {
		_srgbView = view;
	}
	void SetUnormView(vk::ImageView view) {
		_unormView = view;
	}

 private:
	Device& _device;
	vk::ImageView _view;
	ImageViewCreateInfo _createInfo;

	vk::ImageView _depthView;
	vk::ImageView _stencilView;
	vk::ImageView _unormView;
	vk::ImageView _srgbView;
	std::vector<vk::ImageView> _renderTargetViews;
};

struct ImageDeleter {
	void operator()(Image* image);
};

class Image : public IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>, public Cookie, public InternalSync {
 public:
	friend class ObjectPool<Image>;
	friend struct ImageDeleter;

	~Image() noexcept;

	vk::AccessFlags GetAccessFlags() const {
		return _accessFlags;
	}
	const ImageCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::Extent2D GetExtent(uint32_t mip = 0) const {
		return vk::Extent2D(std::max(1u, _createInfo.Width >> mip), std::max(1u, _createInfo.Height >> mip));
	}
	vk::Image GetImage() const {
		return _image;
	}
	vk::ImageLayout GetLayout(vk::ImageLayout optimal) const {
		return _layoutType == ImageLayoutType::Optimal ? optimal : vk::ImageLayout::eGeneral;
	}
	ImageLayoutType GetLayoutType() const {
		return _layoutType;
	}
	vk::PipelineStageFlags GetStageFlags() const {
		return _stageFlags;
	}
	vk::ImageLayout GetSwapchainLayout() const {
		return _swapchainLayout;
	}
	ImageView& GetView() {
		return *_view;
	}
	const ImageView& GetView() const {
		return *_view;
	}
	bool IsSwapchainImage() const {
		return _swapchainLayout != vk::ImageLayout::eUndefined;
	}

	void SetDefaultView(ImageViewHandle view) {
		_view = view;
	}
	void SetSwapchainLayout(vk::ImageLayout layout) {
		_swapchainLayout = layout;
	}

	void DisownImage();
	void DisownMemory();

 private:
	Image(Device& device, vk::Image image, const ImageCreateInfo& imageCI);
	Image(Device& device,
	      vk::Image image,
	      vk::ImageView defaultView,
	      const VmaAllocation& allocation,
	      const ImageCreateInfo& imageCI,
	      vk::ImageViewType viewType);

	Device& _device;
	vk::Image _image;
	ImageViewHandle _view;
	VmaAllocation _allocation;
	ImageCreateInfo _createInfo;

	vk::AccessFlags _accessFlags       = {};
	ImageLayoutType _layoutType        = ImageLayoutType::Optimal;
	vk::PipelineStageFlags _stageFlags = {};
	bool _imageOwned                   = true;
	bool _memoryOwned                  = true;
	vk::ImageLayout _swapchainLayout   = vk::ImageLayout::eUndefined;
};
}  // namespace Vulkan
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::Vulkan::ImageCreateFlagBits> : std::true_type {};
