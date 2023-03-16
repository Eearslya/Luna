#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/TextureFormat.hpp>

namespace Luna {
namespace Vulkan {
struct ImageCreateInfo {
	ImageDomain Domain              = ImageDomain::Physical;
	uint32_t Width                  = 1;
	uint32_t Height                 = 1;
	uint32_t Depth                  = 1;
	uint32_t MipLevels              = 1;
	uint32_t ArrayLayers            = 1;
	vk::Format Format               = vk::Format::eUndefined;
	vk::ImageLayout InitialLayout   = vk::ImageLayout::eGeneral;
	vk::ImageType Type              = vk::ImageType::e2D;
	vk::ImageUsageFlags Usage       = {};
	vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
	vk::ImageCreateFlags Flags      = {};
	ImageCreateFlags MiscFlags      = {};
	vk::ComponentMapping Swizzle    = {};

	static ImageCreateInfo ImmutableImage(const TextureFormatLayout& layout) {
		return ImageCreateInfo{.Domain        = ImageDomain::Physical,
		                       .Width         = layout.GetWidth(),
		                       .Height        = layout.GetHeight(),
		                       .Depth         = layout.GetDepth(),
		                       .MipLevels     = layout.GetMipLevels(),
		                       .ArrayLayers   = layout.GetArrayLayers(),
		                       .Format        = layout.GetFormat(),
		                       .InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		                       .Type          = layout.GetImageType(),
		                       .Usage         = vk::ImageUsageFlagBits::eSampled,
		                       .Samples       = vk::SampleCountFlagBits::e1};
	}
	static ImageCreateInfo Immutable2D(vk::Format format, uint32_t width, uint32_t height, bool generateMips = false) {
		return ImageCreateInfo{.Domain        = ImageDomain::Physical,
		                       .Width         = width,
		                       .Height        = height,
		                       .Depth         = 1,
		                       .MipLevels     = generateMips ? 0u : 1u,
		                       .ArrayLayers   = 1,
		                       .Format        = format,
		                       .InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		                       .Type          = vk::ImageType::e2D,
		                       .Usage         = vk::ImageUsageFlagBits::eSampled,
		                       .Samples       = vk::SampleCountFlagBits::e1,
		                       .MiscFlags     = generateMips ? ImageCreateFlagBits::GenerateMipmaps : ImageCreateFlags{}};
	}
	static ImageCreateInfo Immutable3D(
		vk::Format format, uint32_t width, uint32_t height, uint32_t depth, bool generateMips = false) {
		auto info  = Immutable2D(format, width, height, generateMips);
		info.Depth = depth;
		info.Type  = vk::ImageType::e3D;

		return info;
	}
	static ImageCreateInfo RenderTarget(vk::Format format, uint32_t width, uint32_t height) {
		const bool depthStencil = FormatHasDepthOrStencil(format);

		return ImageCreateInfo{
			.Domain      = ImageDomain::Physical,
			.Width       = width,
			.Height      = height,
			.Depth       = 1,
			.MipLevels   = 1,
			.ArrayLayers = 1,
			.Format      = format,
			.InitialLayout =
				depthStencil ? vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eColorAttachmentOptimal,
			.Type = vk::ImageType::e2D,
			.Usage =
				(depthStencil ? vk::ImageUsageFlagBits::eDepthStencilAttachment : vk::ImageUsageFlagBits::eColorAttachment) |
				vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
			.Samples = vk::SampleCountFlagBits::e1,
		};
	}
	static ImageCreateInfo TransientRenderTarget(vk::Format format, uint32_t width, uint32_t height) {
		const bool depthStencil = FormatHasDepthOrStencil(format);

		return ImageCreateInfo{
			.Domain        = ImageDomain::Transient,
			.Width         = width,
			.Height        = height,
			.Depth         = 1,
			.MipLevels     = 1,
			.ArrayLayers   = 1,
			.Format        = format,
			.InitialLayout = vk::ImageLayout::eUndefined,
			.Type          = vk::ImageType::e2D,
			.Usage =
				(depthStencil ? vk::ImageUsageFlagBits::eDepthStencilAttachment : vk::ImageUsageFlagBits::eColorAttachment) |
				vk::ImageUsageFlagBits::eInputAttachment,
			.Samples = vk::SampleCountFlagBits::e1,
		};
	}

	static std::vector<vk::Format> GetComputeFormats(const ImageCreateInfo& info) {
		if (!(info.MiscFlags & ImageCreateFlagBits::MutableSrgb)) { return {}; }

		switch (info.Format) {
			case vk::Format::eR8G8B8A8Unorm:
			case vk::Format::eR8G8B8A8Srgb:
				return {vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8A8Srgb};

			case vk::Format::eB8G8R8A8Unorm:
			case vk::Format::eB8G8R8A8Srgb:
				return {vk::Format::eB8G8R8A8Unorm, vk::Format::eB8G8R8A8Srgb};

			case vk::Format::eA8B8G8R8UnormPack32:
			case vk::Format::eA8B8G8R8SrgbPack32:
				return {vk::Format::eA8B8G8R8UnormPack32, vk::Format::eA8B8G8R8SrgbPack32};

			default:
				return {};
		}
	}
};

struct ImageViewCreateInfo {
	const Image* Image             = nullptr;
	vk::Format Format              = vk::Format::eUndefined;
	uint32_t BaseLevel             = 0;
	uint32_t MipLevels             = VK_REMAINING_MIP_LEVELS;
	uint32_t BaseLayer             = 0;
	uint32_t ArrayLayers           = VK_REMAINING_ARRAY_LAYERS;
	vk::ImageViewType ViewType     = vk::ImageViewType::e2D;
	vk::ComponentMapping Swizzle   = {};
	ImageViewCreateFlags MiscFlags = {};
};

vk::AccessFlags ImageLayoutToAccess(vk::ImageLayout layout);
vk::AccessFlags2 ImageLayoutToAccess2(vk::ImageLayout layout);
vk::AccessFlags ImageUsageToAccess(vk::ImageUsageFlags usage);
vk::AccessFlags2 ImageUsageToAccess2(vk::ImageUsageFlags usage);
vk::FormatFeatureFlags ImageUsageToFeatures(vk::ImageUsageFlags usage);
vk::PipelineStageFlags ImageUsageToStages(vk::ImageUsageFlags usage);
vk::PipelineStageFlags2 ImageUsageToStages2(vk::ImageUsageFlags usage);

struct ImageDeleter {
	void operator()(Image* image);
};

class Image : public IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>,
							public Cookie,
							public InternalSyncEnabled {
	friend struct ObjectPool<Image>;
	friend struct ImageDeleter;

 public:
	~Image() noexcept;

	const VmaAllocation& GetAllocation() const {
		return _allocation;
	}
	const ImageCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::Image GetImage() const {
		return _image;
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

	uint32_t GetWidth(uint32_t mip = 0) const {
		return std::max(1u, _createInfo.Width >> mip);
	}
	uint32_t GetHeight(uint32_t mip = 0) const {
		return std::max(1u, _createInfo.Height >> mip);
	}
	uint32_t GetDepth(uint32_t mip = 0) const {
		return std::max(1u, _createInfo.Depth >> mip);
	}
	vk::AccessFlags GetAccess() const {
		return _accessFlags;
	}
	vk::ImageLayout GetLayout(vk::ImageLayout optimal) const {
		return _layoutType == ImageLayout::Optimal ? optimal : vk::ImageLayout::eGeneral;
	}
	ImageLayout GetLayoutType() const {
		return _layoutType;
	}
	vk::PipelineStageFlags GetStages() const {
		return _stageFlags;
	}
	bool IsSwapchainImage() const {
		return _swapchainLayout != vk::ImageLayout::eUndefined;
	}

	void DisownImage() {
		_imageOwned = false;
	}
	void DisownMemory() {
		_memoryOwned = false;
	}
	void SetAccess(vk::AccessFlags access) {
		_accessFlags = access;
	}
	void SetLayoutType(ImageLayout type) {
		_layoutType = type;
	}
	void SetStages(vk::PipelineStageFlags stages) {
		_stageFlags = stages;
	}
	void SetSwapchainLayout(vk::ImageLayout layout) {
		_swapchainLayout = layout;
	}

 private:
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
	ImageLayout _layoutType            = ImageLayout::Optimal;
	vk::PipelineStageFlags _stageFlags = {};
	bool _imageOwned                   = true;
	bool _memoryOwned                  = true;
	vk::ImageLayout _swapchainLayout   = vk::ImageLayout::eUndefined;
};

struct ImageViewDeleter {
	void operator()(ImageView* view);
};

class ImageView : public IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>,
									public Cookie,
									public InternalSyncEnabled {
	friend class ObjectPool<ImageView>;
	friend struct ImageViewDeleter;

 public:
	ImageView(Device& device, vk::ImageView view, const ImageViewCreateInfo& viewCI);
	~ImageView() noexcept;

	const ImageViewCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::Format GetFormat() const {
		return _createInfo.Format;
	}
	const Image* GetImage() const {
		return _createInfo.Image;
	}
	vk::ImageView GetView() const {
		return _view;
	}
	vk::ImageView GetFloatView() const {
		return _depthView ? _depthView : _view;
	}
	vk::ImageView GetIntegerView() const {
		return _stencilView ? _stencilView : _view;
	}
	vk::ImageView GetSrgbView() const {
		return _srgbView;
	}
	vk::ImageView GetUnormView() const {
		return _unormView;
	}

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

	vk::ImageView GetRenderTargetView(uint32_t layer) const;
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	uint32_t GetDepth() const;

 private:
	Device& _device;
	vk::ImageView _view;
	ImageViewCreateInfo _createInfo;
	std::vector<vk::ImageView> _renderTargetViews;
	vk::ImageView _depthView;
	vk::ImageView _stencilView;
	vk::ImageView _unormView;
	vk::ImageView _srgbView;
};
}  // namespace Vulkan
}  // namespace Luna
