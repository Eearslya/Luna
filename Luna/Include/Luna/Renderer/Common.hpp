#pragma once

#include <Luna/Renderer/Enums.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <unordered_set>

namespace Luna {
// Forward Declarations.
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
	vk::Format Format   = vk::Format::eUndefined;
	std::string SizeRelativeName; /** Used when SizeClass is InputRelative, determines which resource is used when
	                                 multiplying size. */
	uint32_t Samples             = 1;
	uint32_t Levels              = 1;
	uint32_t Layers              = 1;
	vk::ImageUsageFlags AuxUsage = {};
	AttachmentInfoFlags Flags    = AttachmentInfoFlagBits::Persistent;
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
		return Size != other.Size || Usage != other.Usage || Flags != other.Flags;
	}
};

/**
 * Describes the actual, physical dimensions and characteristics of a resource.
 */
struct ResourceDimensions {
	vk::Format Format = vk::Format::eUndefined;
	BufferInfo BufferInfo;
	uint32_t Width                            = 0;
	uint32_t Height                           = 0;
	uint32_t Depth                            = 1;
	uint32_t Layers                           = 1;
	uint32_t Levels                           = 1;
	uint32_t Samples                          = 1;
	AttachmentInfoFlags Flags                 = AttachmentInfoFlagBits::Persistent;
	vk::SurfaceTransformFlagBitsKHR Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

	vk::ImageUsageFlags ImageUsage = {};
	std::string Name;
	RenderGraphQueueFlags Queues = {};

	/**
	 * Determines whether the resources is "buffer-like", meaning it is either a buffer, storage image, or Proxy resource.
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

		// Regular compute will use Graphics queue instead.
		if (physicalQueues & RenderGraphQueueFlagBits::Compute) { physicalQueues |= RenderGraphQueueFlagBits::Graphics; }
		physicalQueues &= ~RenderGraphQueueFlagBits::Compute;

		return (physicalQueues & (physicalQueues - 1)) != 0;
	}

	bool operator==(const ResourceDimensions& other) const {
		// ImageUsage, Name, and Queues are deliberately excluded from this test.

		return Format == other.Format && Width == other.Width && Height == other.Height && Depth == other.Depth &&
		       Layers == other.Layers && Levels == other.Levels && BufferInfo == other.BufferInfo && Flags == other.Flags &&
		       Transform == other.Transform;
	}

	bool operator!=(const ResourceDimensions& other) const {
		// ImageUsage, Name, and Queues are deliberately excluded from this test.

		return Format != other.Format || Width != other.Width || Height != other.Height || Depth != other.Depth ||
		       Layers != other.Layers || Levels != other.Levels || BufferInfo != other.BufferInfo || Flags != other.Flags ||
		       Transform != other.Transform;
	}
};

/**
 * Describes a resource used in a Render Pass.
 * Includes information about the type of resource, which queues use it, and which Render Passes read and write from it.
 */
class RenderResource {
 public:
	static constexpr uint32_t Unused = ~0u;

	enum class Type { Buffer, Texture, Proxy };

	/**
	 * Initializes a new RenderResource.
	 *
	 * @param type The type of the resource.
	 * @param index The index of the resource, as determined by the owning RenderGraph.
	 */
	RenderResource(Type type, uint32_t index) : _type(type), _index(index) {}
	virtual ~RenderResource() noexcept = default;

	uint32_t GetIndex() const {
		return _index;
	}
	const std::string& GetName() const {
		return _name;
	}
	uint32_t GetPhysicalIndex() const {
		return _physicalIndex;
	}
	std::unordered_set<uint32_t>& GetReadPasses() {
		return _readInPasses;
	}
	const std::unordered_set<uint32_t>& GetReadPasses() const {
		return _readInPasses;
	}
	Type GetType() const {
		return _type;
	}
	RenderGraphQueueFlags GetUsedQueues() const {
		return _usedQueues;
	}
	std::unordered_set<uint32_t>& GetWritePasses() {
		return _writtenInPasses;
	}
	const std::unordered_set<uint32_t>& GetWritePasses() const {
		return _writtenInPasses;
	}

	/**
	 * Adds the given queue to the list of queues this resource is used in.
	 */
	void AddQueue(RenderGraphQueueFlagBits queue) {
		_usedQueues |= queue;
	}

	/**
	 * Adds the given Render Pass to the list of Render Passes that read this resource.
	 */
	void ReadInPass(uint32_t pass) {
		_readInPasses.insert(pass);
	}

	/**
	 * Sets the name of the resource.
	 */
	void SetName(const std::string& name) {
		_name = name;
	}

	/**
	 * Sets the physical index of the resource, after determining aliases.
	 */
	void SetPhysicalIndex(uint32_t index) {
		_physicalIndex = index;
	}

	/**
	 * Adds the given Render Pass to the list of Render Passes that write to this resource.
	 */
	void WrittenInPass(uint32_t pass) {
		_writtenInPasses.insert(pass);
	}

 private:
	Type _type;                                    /** The type of the resource. */
	uint32_t _index;                               /** The index of the resource within its owning RenderGraph. */
	std::string _name;                             /** The name of the resource. */
	uint32_t _physicalIndex = Unused;              /** The physical index of the resource after aliasing. */
	std::unordered_set<uint32_t> _readInPasses;    /** A list of Render Pass indices which read this resource. */
	RenderGraphQueueFlags _usedQueues = {};        /** A bitmask of queues which use this resource. */
	std::unordered_set<uint32_t> _writtenInPasses; /** A list of Render Pass indices which write to this resource. */
};

/**
 * Describes a buffer resource used in a Render Pass.
 * Includes additional information about buffer size and usage.
 */
class RenderBufferResource : public RenderResource {
 public:
	/**
	 * Initializes a new RenderBufferResource.
	 *
	 * @param index The index of the resource, as determined by the owning RenderGraph.
	 */
	explicit RenderBufferResource(uint32_t index) : RenderResource(RenderResource::Type::Buffer, index) {}

	const BufferInfo& GetBufferInfo() const {
		return _bufferInfo;
	}
	vk::BufferUsageFlags GetBufferUsage() const {
		return _bufferUsage;
	}

	/**
	 * Add additional usages to the buffer.
	 */
	void AddBufferUsage(vk::BufferUsageFlags usage) {
		_bufferUsage |= usage;
	}

	void SetBufferInfo(const BufferInfo& info) {
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
	/**
	 * Initializes a new RenderTextureResource.
	 *
	 * @param index The index of the resource, as determined by the owning RenderGraph.
	 */
	explicit RenderTextureResource(uint32_t index) : RenderResource(RenderResource::Type::Texture, index) {}

	AttachmentInfo& GetAttachmentInfo() {
		return _attachmentInfo;
	}
	const AttachmentInfo& GetAttachmentInfo() const {
		return _attachmentInfo;
	}
	vk::ImageUsageFlags GetImageUsage() const {
		return _imageUsage;
	}
	bool GetTransientState() const {
		return _transient;
	}

	/**
	 * Add additional usage to the image.
	 */
	void AddImageUsage(vk::ImageUsageFlags usage) {
		_imageUsage |= usage;
	}

	void SetAttachmentInfo(const AttachmentInfo& info) {
		_attachmentInfo = info;
	}

	/**
	 * Set whether or not the image is a "transient" attachment.
	 */
	void SetTransientState(bool transient) {
		_transient = transient;
	}

 private:
	AttachmentInfo _attachmentInfo;  /** The texture-specific resource information. */
	vk::ImageUsageFlags _imageUsage; /** A bitmask describing how the texture resource is used. */
	bool _transient = false;         /** Whether or not the image is transient. */
};
}  // namespace Luna
