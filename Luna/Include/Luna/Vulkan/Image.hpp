#pragma once

#include <Luna/Utility/EnumClass.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>

namespace Luna {
namespace Vulkan {
static inline uint32_t CalculateMipLevels(const vk::Extent3D& extent) {
	uint32_t longest = std::max(std::max(extent.width, extent.height), extent.depth);

	return static_cast<uint32_t>(std::floor(std::log2(longest))) + 1;
}

static inline vk::AccessFlags ImageLayoutToPossibleAccess(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eInputAttachmentRead | vk::AccessFlagBits::eShaderRead;
		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eInputAttachmentRead;
		case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;
		case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;
		default:
			return static_cast<vk::AccessFlags>(~0u);
	}
}

static inline vk::AccessFlags ImageUsageToAccess(vk::ImageUsageFlags usage) {
	vk::AccessFlags access;

	if (usage & vk::ImageUsageFlagBits::eTransferDst) { access |= vk::AccessFlagBits::eTransferWrite; }
	if (usage & vk::ImageUsageFlagBits::eTransferSrc) { access |= vk::AccessFlagBits::eTransferRead; }
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
		access &= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite |
		          vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite |
		          vk::AccessFlagBits::eInputAttachmentRead;
	}

	return access;
}

static inline vk::PipelineStageFlags ImageUsageToStages(vk::ImageUsageFlags usage) {
	vk::PipelineStageFlags stages;

	if (usage & (vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)) {
		stages |= vk::PipelineStageFlagBits::eTransfer;
	}
	if (usage & vk::ImageUsageFlagBits::eSampled) {
		stages |= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader |
		          vk::PipelineStageFlagBits::eVertexShader;
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

enum class ImageCreateFlagBits { GenerateMipmaps = 1 << 0 };
using ImageCreateFlags = Bitmask<ImageCreateFlagBits>;

enum class ImageDomain { Physical, Transient };

struct ImageCreateInfo {
	static ImageCreateInfo Immutable2D(vk::Format format, const vk::Extent2D& extent, bool mipmapped = false) {
		return {.Format        = format,
		        .Type          = vk::ImageType::e2D,
		        .Usage         = vk::ImageUsageFlagBits::eSampled,
		        .Extent        = vk::Extent3D(extent.width, extent.height, 1),
		        .MipLevels     = mipmapped ? 0u : 1u,
		        .InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		        .Flags         = mipmapped ? ImageCreateFlagBits::GenerateMipmaps : ImageCreateFlags()};
	}

	static ImageCreateInfo RenderTarget(vk::Format format, const vk::Extent2D& extent) {
		return {.Domain        = ImageDomain::Physical,
		        .Format        = format,
		        .Type          = vk::ImageType::e2D,
		        .Usage         = (FormatHasDepthOrStencil(format) ? vk::ImageUsageFlagBits::eDepthStencilAttachment
		                                                          : vk::ImageUsageFlags()),
		        .Extent        = vk::Extent3D(extent.width, extent.height, 1),
		        .ArrayLayers   = 1,
		        .MipLevels     = 1,
		        .Samples       = vk::SampleCountFlagBits::e1,
		        .InitialLayout = FormatHasDepthOrStencil(format) ? vk::ImageLayout::eDepthStencilAttachmentOptimal
		                                                         : vk::ImageLayout::eColorAttachmentOptimal};
	}

	ImageDomain Domain              = ImageDomain::Physical;
	vk::Format Format               = vk::Format::eUndefined;
	vk::ImageType Type              = vk::ImageType::e2D;
	vk::ImageUsageFlags Usage       = {};
	vk::Extent3D Extent             = {};
	uint32_t ArrayLayers            = 1;
	uint32_t MipLevels              = 1;
	vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
	vk::ImageLayout InitialLayout   = vk::ImageLayout::eUndefined;
	ImageCreateFlags Flags          = {};
};

struct ImageViewCreateInfo {
	Image* Image            = nullptr;
	vk::Format Format       = vk::Format::eUndefined;
	vk::ImageViewType Type  = vk::ImageViewType::e2D;
	uint32_t BaseMipLevel   = 0;
	uint32_t MipLevels      = VK_REMAINING_MIP_LEVELS;
	uint32_t BaseArrayLayer = 0;
	uint32_t ArrayLayers    = VK_REMAINING_ARRAY_LAYERS;
};

static inline vk::ImageViewType GetImageViewType(const ImageCreateInfo& createInfo) {
	switch (createInfo.Type) {
		case vk::ImageType::e1D:
			return createInfo.ArrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
		case vk::ImageType::e2D:
			return createInfo.ArrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
		case vk::ImageType::e3D:
			return vk::ImageViewType::e3D;
	}

	assert(false && "Invalid ImageCreateInfo!");
	return vk::ImageViewType::e2D;
}

struct ImageDeleter {
	void operator()(Image* image);
};

struct ImageViewDeleter {
	void operator()(ImageView* view);
};

class Image : public IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>,
							public Cookie,
							public InternalSyncEnabled {
	friend class ObjectPool<Image>;
	friend struct ImageDeleter;

 public:
	~Image() noexcept;

	vk::AccessFlags GetAccessFlags() const {
		return _accessFlags;
	}
	const ImageCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::Image GetImage() const {
		return _image;
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
	const ImageViewHandle& GetView() const {
		return _defaultView;
	}
	bool IsSwapchainImage() const {
		return _swapchainLayout != vk::ImageLayout::eUndefined;
	}

	vk::ImageLayout GetLayout(vk::ImageLayout optimal) const;

	void SetAccessFlags(vk::AccessFlags access) {
		_accessFlags = access;
	}
	void SetDefaultView(const ImageViewHandle& view) {
		_defaultView = view;
	}
	void SetStageFlags(vk::PipelineStageFlags stages) {
		_stageFlags = stages;
	}
	void SetSwapchainLayout(vk::ImageLayout layout) {
		_swapchainLayout = layout;
	}

 private:
	Image(Device& device, const ImageCreateInfo& createInfo);
	Image(Device& device, const ImageCreateInfo& createInfo, vk::Image image);

	Device& _device;
	ImageCreateInfo _createInfo;
	vk::Image _image;
	VmaAllocation _allocation;
	ImageLayoutType _layoutType = ImageLayoutType::Optimal;
	vk::AccessFlags _accessFlags;
	vk::PipelineStageFlags _stageFlags;
	vk::ImageLayout _swapchainLayout = vk::ImageLayout::eUndefined;
	bool _ownsImage                  = true;

	ImageViewHandle _defaultView;
};

class ImageView : public IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>,
									public Cookie,
									public InternalSyncEnabled {
	friend class ObjectPool<ImageView>;
	friend struct ImageViewDeleter;

 public:
	~ImageView() noexcept;

	const ImageViewCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	const Image& GetImage() const {
		return *_createInfo.Image;
	}
	vk::ImageView GetImageView() const {
		return _imageView;
	}

 private:
	ImageView(Device& device, const ImageViewCreateInfo& createInfo);

	Device& _device;
	vk::ImageView _imageView;
	ImageViewCreateInfo _createInfo;
};
}  // namespace Vulkan

template <>
struct EnableBitmaskOperators<Vulkan::ImageCreateFlagBits> : std::true_type {};
}  // namespace Luna
