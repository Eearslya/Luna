#pragma once

#include <Luna/Common.hpp>
#include <Luna/Renderer/Enums.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
class Renderer;
class RenderGraph;
class RenderPass;

/**
 * Describes an image attachment for a Render Pass.
 * Includes information about Format, Size, Mip Levels, and Array Layers.
 * By default, describes an image of the same size and format as the swapchain.
 */
struct AttachmentInfo {
	SizeClass SizeClass = SizeClass::SwapchainRelative;
	float Width         = 1.0f;
	float Height        = 1.0f;
	float Depth         = 0.0f;
	vk::Format Format =
		vk::Format::eUndefined;     /** A format of Undefined will be translated to match the swapchain's format. */
	std::string SizeRelativeName; /** Used when SizeClass is InputRelative, determines which resource is used when
	                                 multiplying size. */
	uint32_t SampleCount         = 1;
	uint32_t MipLevels           = 1;
	uint32_t ArrayLayers         = 1;
	vk::ImageUsageFlags AuxUsage = {};
	AttachmentInfoFlags Flags    = AttachmentInfoFlagBits::Persistent;

	bool operator==(const AttachmentInfo& other) const {
		return SizeClass == other.SizeClass && Width == other.Width && Height == other.Height && Depth == other.Depth &&
		       Format == other.Format && SizeRelativeName == other.SizeRelativeName && SampleCount == other.SampleCount &&
		       MipLevels == other.MipLevels && ArrayLayers == other.ArrayLayers && AuxUsage == other.AuxUsage &&
		       Flags == other.Flags;
	}
	bool operator!=(const AttachmentInfo& other) const {
		return !operator==(other);
	}

	/** Convenience function to create a copy of an existing struct when using the builder pattern. */
	AttachmentInfo Copy() const {
		return *this;
	}

	AttachmentInfo& GenerateMips() {
		Flags |= AttachmentInfoFlagBits::GenerateMips;
		return *this;
	}
	AttachmentInfo& SetDepth(float depth) {
		Depth = depth;
		return *this;
	}
	AttachmentInfo& SetFormat(vk::Format format) {
		Format = format;
		return *this;
	}
	AttachmentInfo& SetHeight(float height) {
		Height = height;
		return *this;
	}
	AttachmentInfo& SetSampleCount(uint32_t sampleCount) {
		SampleCount = sampleCount;
		return *this;
	}
	AttachmentInfo& SetWidth(float width) {
		Width = width;
		return *this;
	}
};

/**
 * Describes a buffer attachment for a Render Pass.
 * Includes information about Size and Usage.
 */
struct BufferInfo {
	vk::DeviceSize Size        = 0;
	vk::BufferUsageFlags Usage = {};
	AttachmentInfoFlags Flags  = AttachmentInfoFlagBits::Persistent;

	bool operator==(const BufferInfo& other) const {
		return Size == other.Size && Usage == other.Usage && Flags == other.Flags;
	}
	bool operator!=(const BufferInfo& other) const {
		return !operator==(other);
	}
};

/**
 * Describes the actual, physical dimensions and characteristics of a resource.
 */
struct ResourceDimensions {
	vk::Format Format                         = vk::Format::eUndefined;
	BufferInfo BufferInfo                     = {};
	uint32_t Width                            = 0;
	uint32_t Height                           = 0;
	uint32_t Depth                            = 1;
	uint32_t ArrayLayers                      = 1;
	uint32_t MipLevels                        = 1;
	uint32_t SampleCount                      = 1;
	AttachmentInfoFlags Flags                 = AttachmentInfoFlagBits::Persistent;
	vk::SurfaceTransformFlagBitsKHR Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

	vk::ImageUsageFlags ImageUsage = {};
	std::string Name;
	RenderGraphQueueFlags Queues = {};

	/**
	 * Determines whether the resource is "buffer-like", meaning it is either a buffer, storage image, or Proxy resource.
	 */
	bool IsBufferLike() const {
		return IsStorageImage() || (BufferInfo.Size != 0) || (Flags & AttachmentInfoFlagBits::InternalProxy);
	}

	/**
	 * Determines whether the resource is a storage image.
	 */
	bool IsStorageImage() const {
		return bool(ImageUsage & vk::ImageUsageFlagBits::eStorage);
	}

	/**
	 * Determines whether this resource is used across multiple queues, and thus requires a semaphore for proper
	 * synchronization.
	 */
	bool UsesSemaphore() const {
		if (Flags & AttachmentInfoFlagBits::InternalProxy) { return true; }

		auto physicalQueues = Queues;
		if (physicalQueues & RenderGraphQueueFlagBits::Compute) { physicalQueues |= RenderGraphQueueFlagBits::Graphics; }
		physicalQueues &= ~RenderGraphQueueFlagBits::Compute;

		return (physicalQueues &
		        RenderGraphQueueFlags(static_cast<decltype(physicalQueues)::UnderlyingT>(physicalQueues) - 1u));
	}

	bool operator==(const ResourceDimensions& other) const {
		// ImageUsage, Name, and Queues are deliberately excluded from this test.

		return Format == other.Format && Width == other.Width && Height == other.Height && Depth == other.Depth &&
		       ArrayLayers == other.ArrayLayers && MipLevels == other.MipLevels && SampleCount == other.SampleCount &&
		       Flags == other.Flags && Transform == other.Transform;
	}

	bool operator!=(const ResourceDimensions& other) const {
		return !operator==(other);
	}
};

/**
 * Describes a resource used in a Render Pass.
 * Includes information about the type of resource, which queues use it, and which Render Passes read from and write to
 * it.
 */
class RenderResource {
 public:
	static constexpr uint32_t Unused = ~0u;

	enum class Type { Buffer, Texture, Proxy };

	RenderResource(Type type, uint32_t index) noexcept : _type(type), _index(index) {}
	virtual ~RenderResource() noexcept = default;

	[[nodiscard]] uint32_t GetIndex() const noexcept {
		return _index;
	}
	[[nodiscard]] const std::string& GetName() const noexcept {
		return _name;
	}
	[[nodiscard]] uint32_t GetPhysicalIndex() const noexcept {
		return _physicalIndex;
	}
	[[nodiscard]] std::unordered_set<uint32_t>& GetReadPasses() noexcept {
		return _readInPasses;
	}
	[[nodiscard]] const std::unordered_set<uint32_t>& GetReadPasses() const noexcept {
		return _readInPasses;
	}
	[[nodiscard]] Type GetType() const noexcept {
		return _type;
	}
	[[nodiscard]] RenderGraphQueueFlags GetUsedQueues() const noexcept {
		return _usedQueues;
	}
	[[nodiscard]] std::unordered_set<uint32_t>& GetWritePasses() noexcept {
		return _writtenInPasses;
	}
	[[nodiscard]] const std::unordered_set<uint32_t>& GetWritePasses() const noexcept {
		return _writtenInPasses;
	}

	void AddQueue(RenderGraphQueueFlagBits queue) noexcept {
		_usedQueues |= queue;
	}
	void ReadInPass(uint32_t pass) {
		_readInPasses.insert(pass);
	}
	void SetName(const std::string& name) {
		_name = name;
	}
	void SetPhysicalIndex(uint32_t index) noexcept {
		_physicalIndex = index;
	}
	void WrittenInPass(uint32_t pass) {
		_writtenInPasses.insert(pass);
	}

 private:
	Type _type;                                    /** The type of the resource. */
	uint32_t _index;                               /** The index of the resource within its owning RenderGraph. */
	std::string _name;                             /** The name of the resource. */
	uint32_t _physicalIndex = Unused;              /** The physical index of the resource, after aliasing. */
	std::unordered_set<uint32_t> _readInPasses;    /** A list of Render Pass indices which read from this resource. */
	RenderGraphQueueFlags _usedQueues = {};        /** A bitmask of queues which use this resource. */
	std::unordered_set<uint32_t> _writtenInPasses; /** A list of Render Pass indices which write to this resource. */
};

/**
 * Describes a buffer resource used in a Render Pass.
 * Includes additional information about buffer size and usage.
 */
class RenderBufferResource : public RenderResource {
 public:
	explicit RenderBufferResource(uint32_t index) : RenderResource(RenderResource::Type::Buffer, index) {}

	[[nodiscard]] const BufferInfo& GetBufferInfo() const noexcept {
		return _bufferInfo;
	}
	[[nodiscard]] vk::BufferUsageFlags GetBufferUsage() const noexcept {
		return _bufferUsage;
	}

	void AddBufferUsage(vk::BufferUsageFlags usage) noexcept {
		_bufferUsage |= usage;
	}
	void SetBufferInfo(const BufferInfo& info) noexcept {
		_bufferInfo = info;
	}

 private:
	BufferInfo _bufferInfo;                 /** The buffer-specific resource information. */
	vk::BufferUsageFlags _bufferUsage = {}; /** A bitmask describing how the buffer resource is used. */
};

/**
 * Describes a texture resource used in a Render Pass.
 * Includes additional information about image format, size, and usage.
 */
class RenderTextureResource : public RenderResource {
 public:
	explicit RenderTextureResource(uint32_t index) : RenderResource(RenderResource::Type::Texture, index) {}

	[[nodiscard]] AttachmentInfo& GetAttachmentInfo() noexcept {
		return _attachmentInfo;
	}
	[[nodiscard]] const AttachmentInfo& GetAttachmentInfo() const noexcept {
		return _attachmentInfo;
	}
	[[nodiscard]] vk::ImageUsageFlags GetImageUsage() const noexcept {
		return _imageUsage;
	}
	[[nodiscard]] bool GetTransientState() const noexcept {
		return _transient;
	}

	void AddImageUsage(vk::ImageUsageFlags usage) noexcept {
		_imageUsage |= usage;
	}
	void SetAttachmentInfo(const AttachmentInfo& info) noexcept {
		_attachmentInfo = info;
	}
	void SetTransientState(bool transient) noexcept {
		_transient = transient;
	}

 private:
	AttachmentInfo _attachmentInfo;          /** The texture-specific resource information. */
	vk::ImageUsageFlags _imageUsage = {};    /** A bitmask describing how the texture resource is used. */
	bool _transient                 = false; /** Whether or not the image is transient. */
};
}  // namespace Luna
