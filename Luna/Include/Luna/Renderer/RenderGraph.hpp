#pragma once

#include <Luna/Utility/EnumClass.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <unordered_set>

namespace Luna {
enum class AttachmentInfoFlagBits {
	Persistent        = 1 << 0,
	UnormSrgbAlias    = 1 << 1,
	SupportsPrerotate = 1 << 2,
	GenerateMips      = 1 << 3,
	InternalTransient = 1 << 16,
	InternalProxy     = 1 << 17
};
using AttachmentInfoFlags = Bitmask<AttachmentInfoFlagBits>;

enum class RenderGraphQueueFlagBits {
	Graphics      = 1 << 0,
	Compute       = 1 << 1,
	AsyncCompute  = 1 << 2,
	AsyncGraphics = 1 << 3
};
using RenderGraphQueueFlags = Bitmask<RenderGraphQueueFlagBits>;

enum class SizeClass { Absolute, SwapchainRelative, InputRelative };
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::AttachmentInfoFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::RenderGraphQueueFlagBits> : std::true_type {};

namespace Luna {
class RenderGraph;
class RenderPass;
class TaskComposer;

struct AttachmentInfo {
	SizeClass SizeClass = SizeClass::SwapchainRelative;
	float Width         = 1.0f;
	float Height        = 1.0f;
	float Depth         = 0.0f;
	vk::Format Format   = vk::Format::eUndefined;
	std::string SizeRelativeName;
	uint32_t Samples             = 1;
	uint32_t Levels              = 1;
	uint32_t Layers              = 1;
	vk::ImageUsageFlags AuxUsage = {};
	AttachmentInfoFlags Flags    = AttachmentInfoFlagBits::Persistent;
};

struct BufferInfo {
	vk::DeviceSize Size        = 0;
	vk::BufferUsageFlags Usage = {};
	AttachmentInfoFlags Flags  = AttachmentInfoFlagBits::Persistent;

	bool operator==(const BufferInfo& other) const {
		return Size == other.Size && Usage == other.Usage && Flags == other.Flags;
	}

	bool operator!=(const BufferInfo& other) const {
		return !(*this == other);
	}
};

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
	RenderGraphQueueFlags Queues              = {};
	vk::ImageUsageFlags ImageUsage            = {};

	std::string Name;

	bool IsBufferLike() const {
		return IsStorageImage() || (BufferInfo.Size != 0) || bool(Flags & AttachmentInfoFlagBits::InternalProxy);
	}

	bool IsStorageImage() const {
		return bool(ImageUsage & vk::ImageUsageFlagBits::eStorage);
	}

	bool UsesSemaphore() const {
		if (Flags & AttachmentInfoFlagBits::InternalProxy) { return true; }

		auto queues = Queues;
		if (queues & RenderGraphQueueFlagBits::Compute) { queues |= RenderGraphQueueFlagBits::Graphics; }
		queues &= ~RenderGraphQueueFlagBits::Compute;

		return queues & (queues - 1);
	}

	bool operator==(const ResourceDimensions& other) const {
		// ImageUsage and Queues are omitted intentionally.

		return Format == other.Format && Width == other.Width && Height == other.Height && Depth == other.Depth &&
		       Layers == other.Layers && Levels == other.Levels && BufferInfo == other.BufferInfo && Flags == other.Flags &&
		       Transform == other.Transform;
	}

	bool operator!=(const ResourceDimensions& other) const {
		return !(*this == other);
	}
};

class RenderPassExternalLockInterface {
 public:
	virtual ~RenderPassExternalLockInterface() = default;

	virtual Vulkan::SemaphoreHandle ExternalAcquire()               = 0;
	virtual void ExternalRelease(Vulkan::SemaphoreHandle semaphore) = 0;
};

class RenderResource {
 public:
	enum class Type { Buffer, Texture, Proxy };
	static constexpr uint32_t Unused = ~0u;

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
		return _readPasses;
	}
	const std::unordered_set<uint32_t>& GetReadPasses() const {
		return _readPasses;
	}
	Type GetType() const {
		return _type;
	}
	RenderGraphQueueFlags GetUsedQueues() const {
		return _usedQueues;
	}
	std::unordered_set<uint32_t>& GetWritePasses() {
		return _writePasses;
	}
	const std::unordered_set<uint32_t>& GetWritePasses() const {
		return _writePasses;
	}

	void AddQueue(RenderGraphQueueFlagBits queue) {
		_usedQueues |= queue;
	}
	void ReadInPass(uint32_t index) {
		_readPasses.insert(index);
	}
	void SetName(const std::string& name) {
		_name = name;
	}
	void SetPhysicalIndex(uint32_t index) {
		_physicalIndex = index;
	}
	void WrittenInPass(uint32_t index) {
		_writePasses.insert(index);
	}

 private:
	Type _type;
	std::string _name;
	uint32_t _index;
	uint32_t _physicalIndex = Unused;
	std::unordered_set<uint32_t> _readPasses;
	std::unordered_set<uint32_t> _writePasses;
	RenderGraphQueueFlags _usedQueues = {};
};

class RenderBufferResource : public RenderResource {
 public:
	explicit RenderBufferResource(uint32_t index) : RenderResource(RenderResource::Type::Buffer, index) {}

	const BufferInfo& GetBufferInfo() const {
		return _bufferInfo;
	}
	vk::BufferUsageFlags GetBufferUsage() const {
		return _bufferUsage;
	}

	void AddBufferUsage(vk::BufferUsageFlags usage) {
		_bufferUsage |= usage;
	}
	void SetBufferInfo(const BufferInfo& info) {
		_bufferInfo = info;
	}

 private:
	BufferInfo _bufferInfo;
	vk::BufferUsageFlags _bufferUsage = {};
};

class RenderTextureResource : public RenderResource {
 public:
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

	void AddImageUsage(vk::ImageUsageFlags flags) {
		_imageUsage |= flags;
	}
	void SetAttachmentInfo(const AttachmentInfo& info) {
		_attachmentInfo = info;
	}
	void SetTransientState(bool transient) {
		_transient = true;
	}

 private:
	AttachmentInfo _attachmentInfo;
	vk::ImageUsageFlags _imageUsage = {};
	bool _transient                 = false;
};

class RenderPassInterface : public IntrusivePtrEnabled<RenderPassInterface> {
 public:
	virtual ~RenderPassInterface() = default;

	virtual bool RenderPassIsConditional() const;
	virtual bool RenderPassIsSeparateLayered() const;

	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const;
	virtual bool GetClearDepthStencil(vk::ClearDepthStencilValue* value) const;
	virtual bool NeedRenderPass() const;

	virtual void EnqueuePrepareRenderPass(RenderGraph& graph, TaskComposer& composer) {}
	virtual void Setup(Vulkan::Device& device) {}
	virtual void SetupDependencies(RenderPass& self, RenderGraph& graph) {}

	virtual void BuildRenderPass(Vulkan::CommandBuffer& cmd) {}
	virtual void BuildRenderPassSeparateLayer(Vulkan::CommandBuffer& cmd, uint32_t layer) {}
};
using RenderPassInterfaceHandle = IntrusivePtr<RenderPassInterface>;

class RenderPass {
 public:
	static constexpr uint32_t Unused = ~0u;

	struct AccessedResource {
		vk::AccessFlags2 Access        = {};
		vk::ImageLayout Layout         = vk::ImageLayout::eUndefined;
		vk::PipelineStageFlags2 Stages = {};
	};
	struct AccessedBufferResource : AccessedResource {
		RenderBufferResource* Buffer = nullptr;
	};
	struct AccessedExternalLockInterface : AccessedResource {
		RenderPassExternalLockInterface* Interface = nullptr;
		vk::PipelineStageFlags2 ExternalStages     = {};
	};
	struct AccessedProxyResource : AccessedResource {
		RenderResource* Proxy = nullptr;
	};
	struct AccessedTextureResource : AccessedResource {
		RenderTextureResource* Texture = nullptr;
	};

	RenderPass(RenderGraph& graph, uint32_t index, RenderGraphQueueFlagBits queue)
			: _graph(graph), _index(index), _queue(queue) {}

	RenderGraph& GetGraph() {
		return _graph;
	}
	uint32_t GetIndex() const {
		return _index;
	}
	const std::string& GetName() const {
		return _name;
	}
	uint32_t GetPhysicalPassIndex() const {
		return _physicalPass;
	}
	RenderGraphQueueFlagBits GetQueue() const {
		return _queue;
	}

	const std::vector<RenderTextureResource*> GetAttachmentInputs() const {
		return _attachmentInputs;
	}
	const std::vector<RenderTextureResource*> GetBlitTextureInputs() const {
		return _blitTextureInputs;
	}
	const std::vector<RenderTextureResource*> GetBlitTextureOutputs() const {
		return _blitTextureOutputs;
	}
	const std::vector<RenderTextureResource*> GetColorInputs() const {
		return _colorInputs;
	}
	const std::vector<RenderTextureResource*> GetColorScaleInputs() const {
		return _colorScaleInputs;
	}
	const std::vector<RenderTextureResource*> GetColorOutputs() const {
		return _colorOutputs;
	}
	const std::vector<RenderTextureResource*> GetHistoryInputs() const {
		return _historyInputs;
	}
	const std::vector<RenderTextureResource*> GetResolveOutputs() const {
		return _resolveOutputs;
	}
	const std::vector<RenderBufferResource*> GetStorageInputs() const {
		return _storageInputs;
	}
	const std::vector<RenderBufferResource*> GetStorageOutputs() const {
		return _storageOutputs;
	}
	const std::vector<RenderTextureResource*> GetStorageTextureInputs() const {
		return _storageTextureInputs;
	}
	const std::vector<RenderTextureResource*> GetStorageTextureOutputs() const {
		return _storageTextureOutputs;
	}
	const std::vector<RenderBufferResource*> GetTransferOutputs() const {
		return _transferOutputs;
	}

	RenderTextureResource* GetDepthStencilInput() const {
		return _depthStencilInput;
	}
	RenderTextureResource* GetDepthStencilOutput() const {
		return _depthStencilOutput;
	}
	const std::vector<AccessedBufferResource>& GetGenericBufferInputs() const {
		return _genericBuffers;
	}
	const std::vector<AccessedTextureResource>& GetGenericTextureInputs() const {
		return _genericTextures;
	}
	const std::vector<AccessedExternalLockInterface>& GetLockInterfaces() const {
		return _lockInterfaces;
	}
	const std::vector<AccessedProxyResource>& GetProxyInputs() const {
		return _proxyInputs;
	}
	const std::vector<AccessedProxyResource>& GetProxyOutputs() const {
		return _proxyOutputs;
	}

	const std::vector<std::pair<RenderTextureResource*, RenderTextureResource*>>& GetFakeResourceAliases() const {
		return _fakeResourceAlias;
	}

	void SetBuildRenderPass(std::function<void(Vulkan::CommandBuffer&)> func) {
		_buildRenderPass = func;
	}
	void SetGetClearColor(std::function<bool(uint32_t, vk::ClearColorValue*)> func) {
		_getClearColor = func;
	}
	void SetGetClearDepthStencil(std::function<bool(vk::ClearDepthStencilValue*)> func) {
		_getClearDepthStencil = func;
	}
	void SetName(const std::string& name) {
		_name = name;
	}
	void SetRenderPassInterface(RenderPassInterfaceHandle handle) {
		_renderPassHandle = std::move(handle);
	}
	void SetPhysicalPassIndex(uint32_t index) {
		_physicalPass = index;
	}

	void BuildRenderPass(Vulkan::CommandBuffer& cmd, uint32_t layer) {
		if (_renderPassHandle) {
			if (_renderPassHandle->RenderPassIsSeparateLayered()) {
				_renderPassHandle->BuildRenderPassSeparateLayer(cmd, layer);
			} else {
				_renderPassHandle->BuildRenderPass(cmd);
			}
		} else if (_buildRenderPass) {
			_buildRenderPass(cmd);
		}
	}
	bool GetClearColor(uint32_t index, vk::ClearColorValue* value = nullptr) const {
		if (_renderPassHandle) {
			return _renderPassHandle->GetClearColor(index, value);
		} else if (_getClearColor) {
			return _getClearColor(index, value);
		}
		return false;
	}
	bool GetClearDepthStencil(vk::ClearDepthStencilValue* value = nullptr) const {
		if (_renderPassHandle) {
			return _renderPassHandle->GetClearDepthStencil(value);
		} else if (_getClearDepthStencil) {
			return _getClearDepthStencil(value);
		}
		return false;
	}
	void MakeColorInputScaled(uint32_t index) {
		std::swap(_colorScaleInputs[index], _colorInputs[index]);
	}
	bool MayNotNeedRenderPass() const {
		if (_renderPassHandle) { return _renderPassHandle->RenderPassIsConditional(); }
		return false;
	}
	bool NeedRenderPass() const {
		if (_renderPassHandle) { return _renderPassHandle->NeedRenderPass(); }
		return true;
	}
	void PrepareRenderPass(TaskComposer& composer) {
		if (_renderPassHandle) { _renderPassHandle->EnqueuePrepareRenderPass(_graph, composer); }
	}
	bool RenderPassIsMultiview() const {
		if (_renderPassHandle) { return !_renderPassHandle->RenderPassIsSeparateLayered(); }
		return true;
	}
	void Setup(Vulkan::Device& device) {
		if (_renderPassHandle) { _renderPassHandle->Setup(device); }
	}
	void SetupDependencies() {
		if (_renderPassHandle) { _renderPassHandle->SetupDependencies(*this, _graph); }
	}

 private:
	RenderGraph& _graph;
	uint32_t _index;
	std::string _name;
	uint32_t _physicalPass = Unused;
	RenderGraphQueueFlagBits _queue;

	std::function<void(Vulkan::CommandBuffer&)> _buildRenderPass;
	std::function<bool(uint32_t, vk::ClearColorValue*)> _getClearColor;
	std::function<bool(vk::ClearDepthStencilValue*)> _getClearDepthStencil;
	RenderPassInterfaceHandle _renderPassHandle;

	std::vector<RenderTextureResource*> _attachmentInputs;
	std::vector<RenderTextureResource*> _blitTextureInputs;
	std::vector<RenderTextureResource*> _blitTextureOutputs;
	std::vector<RenderTextureResource*> _colorInputs;
	std::vector<RenderTextureResource*> _colorScaleInputs;
	std::vector<RenderTextureResource*> _colorOutputs;
	std::vector<RenderTextureResource*> _historyInputs;
	std::vector<RenderTextureResource*> _resolveOutputs;
	std::vector<RenderBufferResource*> _storageInputs;
	std::vector<RenderBufferResource*> _storageOutputs;
	std::vector<RenderTextureResource*> _storageTextureInputs;
	std::vector<RenderTextureResource*> _storageTextureOutputs;
	std::vector<RenderBufferResource*> _transferOutputs;

	RenderTextureResource* _depthStencilInput  = nullptr;
	RenderTextureResource* _depthStencilOutput = nullptr;
	std::vector<AccessedBufferResource> _genericBuffers;
	std::vector<AccessedTextureResource> _genericTextures;
	std::vector<AccessedExternalLockInterface> _lockInterfaces;
	std::vector<AccessedProxyResource> _proxyInputs;
	std::vector<AccessedProxyResource> _proxyOutputs;

	std::vector<std::pair<RenderTextureResource*, RenderTextureResource*>> _fakeResourceAlias;
};

class RenderGraph {
 public:
	RenderGraph(Vulkan::Device& device);
	~RenderGraph() noexcept;

	void Bake();
	std::vector<Vulkan::BufferHandle> ConsumePhysicalBuffers() const;
	void Reset();
	void SetBackbufferDimensions(const ResourceDimensions& dim);

 private:
	struct PhysicalPass {};

	struct PipelineEvent {};

	Vulkan::Device& _device;

	ResourceDimensions _backbufferDimensions;
	std::unordered_map<std::string, RenderPassExternalLockInterface*> _externalLockInterfaces;
	std::vector<Vulkan::ImageView*> _physicalAttachments;
	std::vector<Vulkan::BufferHandle> _physicalBuffers;
	std::vector<ResourceDimensions> _physicalDimensions;
	std::vector<PipelineEvent> _physicalEvents;
	std::vector<PipelineEvent> _physicalHistoryEvents;
	std::vector<Vulkan::ImageHandle> _physicalHistoryImageAttachments;
	std::vector<Vulkan::ImageHandle> _physicalImageAttachments;
	std::vector<PhysicalPass> _physicalPasses;
	std::vector<std::unique_ptr<RenderPass>> _renderPasses;
	std::unordered_map<std::string, uint32_t> _renderPassToIndex;
	std::vector<std::unique_ptr<RenderResource>> _renderResources;
	std::unordered_map<std::string, uint32_t> _renderResourceToIndex;
};
}  // namespace Luna
