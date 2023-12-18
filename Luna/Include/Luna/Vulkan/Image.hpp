#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>

namespace Luna {
namespace Vulkan {
struct ImageDeleter {
	void operator()(Image* image);
};

struct ImageInitialData {
	ImageInitialData() = default;
	ImageInitialData(const void* data, uint32_t rowLength = 0, uint32_t imageHeight = 0)
			: Data(data), RowLength(rowLength), ImageHeight(imageHeight) {}

	const void* Data;
	uint32_t RowLength   = 0;
	uint32_t ImageHeight = 0;
};

struct ImageCreateInfo {
	constexpr ImageCreateInfo& SetDomain(ImageDomain domain) noexcept {
		Domain = domain;

		return *this;
	}
	constexpr ImageCreateInfo& SetWidth(uint32_t width) noexcept {
		Width = width;

		return *this;
	}
	constexpr ImageCreateInfo& SetHeight(uint32_t height) noexcept {
		Height = height;

		return *this;
	}
	constexpr ImageCreateInfo& SetDepth(uint32_t depth) noexcept {
		Depth = depth;

		return *this;
	}
	constexpr ImageCreateInfo& SetExtent(uint32_t extent) noexcept {
		Type   = vk::ImageType::e1D;
		Width  = extent;
		Height = 1;
		Depth  = 1;

		return *this;
	}
	constexpr ImageCreateInfo& SetExtent(vk::Extent2D extent) noexcept {
		Type   = vk::ImageType::e2D;
		Width  = extent.width;
		Height = extent.height;
		Depth  = 1;

		return *this;
	}
	constexpr ImageCreateInfo& SetExtent(vk::Extent3D extent) noexcept {
		Type   = vk::ImageType::e3D;
		Width  = extent.width;
		Height = extent.height;
		Depth  = extent.depth;

		return *this;
	}
	constexpr ImageCreateInfo& SetMipLevels(uint32_t mipLevels) noexcept {
		MipLevels = mipLevels;

		return *this;
	}
	constexpr ImageCreateInfo& SetArrayLayers(uint32_t arrayLayers) noexcept {
		ArrayLayers = arrayLayers;

		return *this;
	}
	constexpr ImageCreateInfo& SetFormat(vk::Format format) noexcept {
		Format = format;

		return *this;
	}
	constexpr ImageCreateInfo& SetInitialLayout(vk::ImageLayout initialLayout) noexcept {
		InitialLayout = initialLayout;

		return *this;
	}
	constexpr ImageCreateInfo& SetType(vk::ImageType type) noexcept {
		Type = type;

		return *this;
	}
	constexpr ImageCreateInfo& SetUsage(vk::ImageUsageFlags usage) noexcept {
		Usage = usage;

		return *this;
	}
	constexpr ImageCreateInfo& AddUsage(vk::ImageUsageFlags usage) noexcept {
		Usage |= usage;

		return *this;
	}
	constexpr ImageCreateInfo& SetSamples(vk::SampleCountFlagBits samples) noexcept {
		Samples = samples;

		return *this;
	}
	constexpr ImageCreateInfo& SetFlags(vk::ImageCreateFlags flags) noexcept {
		Flags = flags;

		return *this;
	}
	constexpr ImageCreateInfo& AddFlags(vk::ImageCreateFlags flags) noexcept {
		Flags |= flags;

		return *this;
	}
	constexpr ImageCreateInfo& SetMiscFlags(ImageCreateFlags flags) noexcept {
		MiscFlags = flags;

		return *this;
	}
	constexpr ImageCreateInfo& AddMiscFlags(ImageCreateFlags flags) noexcept {
		MiscFlags |= flags;

		return *this;
	}
	constexpr ImageCreateInfo& SetSwizzle(vk::ComponentMapping swizzle) noexcept {
		Swizzle = swizzle;

		return *this;
	}

	constexpr static ImageCreateInfo Immutable2D(vk::Format format,
	                                             uint32_t width,
	                                             uint32_t height,
	                                             bool generateMips = false) {
		return Immutable2D(format, vk::Extent2D(width, height), generateMips);
	}

	constexpr static ImageCreateInfo Immutable2D(vk::Format format, vk::Extent2D extent, bool generateMips = false) {
		return ImageCreateInfo()
		  .SetExtent(extent)
		  .SetFormat(format)
		  .SetMipLevels(generateMips ? 0 : 1)
		  .SetInitialLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
		  .SetUsage(vk::ImageUsageFlagBits::eSampled)
		  .SetMiscFlags(generateMips ? ImageCreateFlagBits::GenerateMipmaps : ImageCreateFlags{});
	}

	constexpr static ImageCreateInfo RenderTarget(vk::Format format, uint32_t width, uint32_t height) {
		return RenderTarget(format, vk::Extent2D(width, height));
	}

	constexpr static ImageCreateInfo RenderTarget(vk::Format format, vk::Extent2D extent) {
		const bool depthStencil = FormatHasDepthOrStencil(format);

		return ImageCreateInfo()
		  .SetExtent(extent)
		  .SetFormat(format)
		  .SetInitialLayout(depthStencil ? vk::ImageLayout::eDepthStencilAttachmentOptimal
		                                 : vk::ImageLayout::eColorAttachmentOptimal)
		  .SetUsage(depthStencil ? vk::ImageUsageFlagBits::eDepthStencilAttachment
		                         : vk::ImageUsageFlagBits::eColorAttachment)
		  .AddUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc);
	}

	constexpr static ImageCreateInfo TransientRenderTarget(vk::Format format, uint32_t width, uint32_t height) {
		return TransientRenderTarget(format, vk::Extent2D(width, height));
	}

	constexpr static ImageCreateInfo TransientRenderTarget(vk::Format format, vk::Extent2D extent) {
		const bool depthStencil = FormatHasDepthOrStencil(format);

		return ImageCreateInfo()
		  .SetDomain(ImageDomain::Transient)
		  .SetExtent(extent)
		  .SetFormat(format)
		  .SetInitialLayout(vk::ImageLayout::eUndefined)
		  .SetUsage(depthStencil ? vk::ImageUsageFlagBits::eDepthStencilAttachment
		                         : vk::ImageUsageFlagBits::eColorAttachment)
		  .AddUsage(vk::ImageUsageFlagBits::eInputAttachment);
	}

	ImageDomain Domain              = ImageDomain::Physical;
	uint32_t Width                  = 1;
	uint32_t Height                 = 1;
	uint32_t Depth                  = 1;
	uint32_t MipLevels              = 1;
	uint32_t ArrayLayers            = 1;
	vk::Format Format               = vk::Format::eUndefined;
	vk::ImageLayout InitialLayout   = vk::ImageLayout::eUndefined;
	vk::ImageType Type              = vk::ImageType::e2D;
	vk::ImageUsageFlags Usage       = {};
	vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
	vk::ImageCreateFlags Flags      = {};
	ImageCreateFlags MiscFlags      = {};
	vk::ComponentMapping Swizzle    = {};
};

class Image : public VulkanObject<Image, ImageDeleter>, public Cookie, public InternalSyncEnabled {
	friend class ObjectPool<Image>;
	friend struct ImageDeleter;

 public:
	~Image() noexcept;

	[[nodiscard]] const ImageCreateInfo GetCreateInfo() const noexcept {
		return _createInfo;
	}
	[[nodiscard]] vk::Image GetImage() const noexcept {
		return _image;
	}
	[[nodiscard]] vk::ImageLayout GetLayout(vk::ImageLayout optimal) const noexcept {
		return _layoutType == ImageLayoutType::Optimal ? optimal : vk::ImageLayout::eGeneral;
	}
	[[nodiscard]] ImageLayoutType GetLayoutType() const noexcept {
		return _layoutType;
	}
	[[nodiscard]] vk::ImageLayout GetSwapchainLayout() const noexcept {
		return _swapchainLayout;
	}
	[[nodiscard]] ImageView& GetView() noexcept {
		return *_imageView;
	}
	[[nodiscard]] const ImageView& GetView() const noexcept {
		return *_imageView;
	}
	[[nodiscard]] const bool IsSwapchainImage() const noexcept {
		return _swapchainLayout != vk::ImageLayout::eUndefined;
	}

	void DisownImage() noexcept;
	void DisownMemory() noexcept;
	void SetLayoutType(ImageLayoutType type) noexcept;
	void SetSwapchainLayout(vk::ImageLayout layout) noexcept;

	static constexpr vk::ImageViewType GetImageViewType(const ImageCreateInfo& imageCI,
	                                                    const ImageViewCreateInfo* viewCI);

 private:
	Image(Device& device,
	      const ImageCreateInfo& createInfo,
	      const ImageInitialData* initialData,
	      const std::string debugName = "");
	Image(Device& device,
	      const ImageCreateInfo& createInfo,
	      vk::Image image,
	      VmaAllocation allocation,
	      vk::ImageView imageView,
	      const std::string& debugName = "");

	Device& _device;
	std::string _debugName;
	ImageCreateInfo _createInfo;
	vk::Image _image;
	VmaAllocation _allocation;
	bool _imageOwned                 = true;
	bool _allocationOwned            = true;
	vk::ImageLayout _swapchainLayout = vk::ImageLayout::eUndefined;
	ImageLayoutType _layoutType      = ImageLayoutType::Optimal;

	ImageViewHandle _imageView;
};

struct ImageViewDeleter {
	void operator()(ImageView* imageView);
};

struct ImageViewCreateInfo {
	Image* Image                   = nullptr;
	vk::Format Format              = vk::Format::eUndefined;
	uint32_t BaseLevel             = 0;
	uint32_t MipLevels             = VK_REMAINING_MIP_LEVELS;
	uint32_t BaseLayer             = 0;
	uint32_t ArrayLayers           = VK_REMAINING_ARRAY_LAYERS;
	vk::ImageViewType ViewType     = vk::ImageViewType::e2D;
	vk::ComponentMapping Swizzle   = {};
	ImageViewCreateFlags MiscFlags = {};
};

class ImageView : public VulkanObject<ImageView, ImageViewDeleter>, public Cookie, public InternalSyncEnabled {
	friend class ObjectPool<ImageView>;
	friend class Image;
	friend struct ImageViewDeleter;

 public:
	~ImageView() noexcept;

	[[nodiscard]] const ImageViewCreateInfo& GetCreateInfo() const noexcept {
		return _createInfo;
	}
	[[nodiscard]] vk::Format GetFormat() const noexcept {
		return _createInfo.Format;
	}
	[[nodiscard]] uint32_t GetHeight() const noexcept {
		return _createInfo.Image->GetCreateInfo().Height;
	}
	[[nodiscard]] Image& GetImage() noexcept {
		return *_createInfo.Image;
	}
	[[nodiscard]] const Image& GetImage() const noexcept {
		return *_createInfo.Image;
	}
	[[nodiscard]] vk::ImageView GetView() const noexcept {
		return _view;
	}
	[[nodiscard]] vk::ImageView GetFloatView() const noexcept {
		return _depthView ? _depthView : _view;
	}
	[[nodiscard]] vk::ImageView GetIntegerView() const noexcept {
		return _stencilView ? _stencilView : _view;
	}
	[[nodiscard]] vk::ImageView GetSrgbView() const noexcept {
		return _srgbView;
	}
	[[nodiscard]] vk::ImageView GetUnormView() const noexcept {
		return _unormView;
	}
	[[nodiscard]] uint32_t GetWidth() const noexcept {
		return _createInfo.Image->GetCreateInfo().Width;
	}

	[[nodiscard]] vk::ImageView GetRenderTargetView(uint32_t layer) const noexcept;

 private:
	ImageView(Device& device, const ImageViewCreateInfo& createInfo);
	ImageView(Device& device, vk::ImageView view, const ImageViewCreateInfo& createInfo);

	Device& _device;
	ImageViewCreateInfo _createInfo;
	vk::ImageView _view;
	std::vector<vk::ImageView> _renderTargetViews;
	vk::ImageView _depthView;
	vk::ImageView _stencilView;
	vk::ImageView _unormView;
	vk::ImageView _srgbView;
};
}  // namespace Vulkan
}  // namespace Luna
