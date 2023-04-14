#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Utility/Threading.hpp>

namespace Luna {
/**
 * A helper class used to manage all of the lambdas usually required to set up and build a RenderPass.
 */
class RenderPassInterface : public IntrusivePtrEnabled<RenderPassInterface> {
 public:
	virtual ~RenderPassInterface() = default;

	// "Fixed" functions, must always return the same results for the life of the interface.
	/**
	 * Determines whether or not this Render Pass may be disabled or enabled each frame.
	 * This function must always return the same result.
	 *
	 * @return true is this Render Pass is conditional, false otherwise.
	 */
	virtual bool RenderPassIsConditional() const;

	/**
	 * Determines whether this Render Pass has separate layers or not.
	 * This function must always return the same result.
	 *
	 * @return true if this Render Pass has separate layers, false otherwise.
	 */
	virtual bool RenderPassIsSeparateLayered() const;

	// Dynamic functions, results may change per frame.
	/**
	 * Retrieves the color clear value for a given attachment.
	 * This function can change per frame.
	 *
	 * @param attachment The index of the attachment.
	 * @param value A pointer to where the clear color will be stored, or nullptr.
	 *
	 * @return true if the given attachment should be cleared, false otherwise.
	 */
	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const;

	/**
	 * Retrieves the depth/stencil clear value.
	 * This function can change per frame.
	 *
	 * @param value A pointer to where the clear value will be stored, or nullptr.
	 *
	 * @return true if the depth/stencil attachment should be cleared, false otherwise.
	 */
	virtual bool GetClearDepthStencil(vk::ClearDepthStencilValue* value) const;

	/**
	 * Determine whether or not this Render Pass to run this frame.
	 * This function can change per frame.
	 *
	 * @return true if the Render Pass should run, false otherwise.
	 */
	virtual bool NeedRenderPass() const;

	// Setup functions, called during Render Graph baking.
	/**
	 * Set up any necessary dependencies.
	 * This function is called once before the Render Graph is baked.
	 *
	 * @param pass The Render Pass object.
	 * @param graph The Render Graph object.
	 */
	virtual void SetupDependencies(RenderPass& pass, RenderGraph& graph);

	/**
	 * Perform any needed post setup.
	 * This function is called once after the Render Graph is baked.
	 *
	 * @param device The Vulkan Device.
	 */
	virtual void Setup();

	/**
	 * Records the Vulkan commands that should run during this Render Pass.
	 * All commands will be executed within a subpass.
	 *
	 * @param cmd The Vulkan Command Buffer.
	 */
	virtual void BuildRenderPass(Vulkan::CommandBuffer& cmd);

	/**
	 * Records the Vulkan command that should run during this Render Pass, for the specified layer.
	 * All commands will be executed within a subpass.
	 *
	 * @param cmd The Vulkan Command Buffer.
	 * @param layer The layer of the Render Pass to record for.
	 */
	virtual void BuildRenderPassSeparateLayer(Vulkan::CommandBuffer& cmd, uint32_t layer);

	/**
	 * Called every frame to allow the Render Pass a chance to prepare dependent resources.
	 *
	 * @param graph The Render Graph object.
	 * @param composer The Task Composer object used for queueing tasks during this frame.
	 */
	virtual void EnqueuePrepareRenderPass(RenderGraph& graph, TaskComposer& composer);
};
using RenderPassInterfaceHandle = IntrusivePtr<RenderPassInterface>;

/**
 * Represents a single Render Pass within the overall Render Graph.
 * Contains information about what buffers and textures will be read from and written to.
 */
class RenderPass {
 public:
	static constexpr uint32_t Unused = ~0u;

	/**
	 * Describes a resource's accesses, including what stages it was used in, what accesses are required, and what layout
	 * it must be in.
	 */
	struct AccessedResource {
		vk::PipelineStageFlags2 Stages = {};
		vk::AccessFlags2 Access        = {};
		vk::ImageLayout Layout         = vk::ImageLayout::eUndefined;
	};

	/**
	 * Describes a Buffer resource's accesses.
	 */
	struct AccessedBufferResource : AccessedResource {
		RenderBufferResource* Buffer = nullptr;
	};

	/**
	 * Describes a Proxy resource's accesses.
	 */
	struct AccessedProxyResource : AccessedResource {
		RenderResource* Proxy = nullptr;
	};

	/**
	 * Describes a Texture resource's accesses.
	 */
	struct AccessedTextureResource : AccessedResource {
		RenderTextureResource* Texture = nullptr;
	};

	/**
	 * Initializes a new RenderPass object.
	 *
	 * @param graph The Render Graph which owns this pass.
	 * @param index The index of this Render Pass.
	 * @param queue Which Queue this Render Pass runs on.
	 */
	RenderPass(RenderGraph& graph, uint32_t index, RenderGraphQueueFlagBits queue);

	RenderGraph& GetGraph() {
		return _graph;
	}
	uint32_t GetIndex() const {
		return _index;
	}
	uint32_t GetPhysicalPassIndex() const {
		return _physicalPass;
	}
	const std::string& GetName() const {
		return _name;
	}
	RenderGraphQueueFlagBits GetQueue() const {
		return _queue;
	}

	const std::vector<AccessedBufferResource>& GetGenericBufferInputs() const {
		return _genericBuffers;
	}
	const std::vector<AccessedTextureResource>& GetGenericTextureInputs() const {
		return _genericTextures;
	}
	const std::vector<AccessedProxyResource>& GetProxyInputs() const {
		return _proxyInputs;
	}
	const std::vector<AccessedProxyResource>& GetProxyOutputs() const {
		return _proxyOutputs;
	}

	const std::vector<RenderBufferResource*>& GetStorageInputs() const {
		return _storageInputs;
	}
	const std::vector<RenderBufferResource*>& GetStorageOutputs() const {
		return _storageOutputs;
	}
	const std::vector<RenderBufferResource*>& GetTransferOutputs() const {
		return _transferOutputs;
	}

	const std::vector<RenderTextureResource*>& GetAttachmentInputs() const {
		return _attachmentInputs;
	}
	const std::vector<RenderTextureResource*>& GetBlitTextureInputs() const {
		return _blitTextureInputs;
	}
	const std::vector<RenderTextureResource*>& GetBlitTextureOutputs() const {
		return _blitTextureOutputs;
	}
	const std::vector<RenderTextureResource*>& GetColorInputs() const {
		return _colorInputs;
	}
	const std::vector<RenderTextureResource*>& GetColorOutputs() const {
		return _colorOutputs;
	}
	const std::vector<RenderTextureResource*>& GetColorScaleInputs() const {
		return _colorScaleInputs;
	}
	RenderTextureResource* GetDepthStencilInput() const {
		return _depthStencilInput;
	}
	RenderTextureResource* GetDepthStencilOutput() const {
		return _depthStencilOutput;
	}
	const std::vector<RenderTextureResource*>& GetHistoryInputs() const {
		return _historyInputs;
	}
	const std::vector<RenderTextureResource*>& GetResolveOutputs() const {
		return _resolveOutputs;
	}
	const std::vector<RenderTextureResource*>& GetStorageTextureInputs() const {
		return _storageTextureInputs;
	}
	const std::vector<RenderTextureResource*>& GetStorageTextureOutputs() const {
		return _storageTextureOutputs;
	}

	const std::vector<std::pair<RenderTextureResource*, RenderTextureResource*>>& GetFakeResourceAliases() const {
		return _fakeResourceAliases;
	}

	RenderTextureResource& AddAttachmentInput(const std::string& name);
	RenderTextureResource& AddBlitTextureReadOnlyInput(const std::string& name);
	RenderTextureResource& AddBlitTextureOutput(const std::string& name,
	                                            const AttachmentInfo& info,
	                                            const std::string& input = "");
	RenderTextureResource& AddColorOutput(const std::string& name,
	                                      const AttachmentInfo& info,
	                                      const std::string& input = "");
	RenderTextureResource& AddHistoryInput(const std::string& name);
	RenderTextureResource& AddResolveOutput(const std::string& name, const AttachmentInfo& info);
	RenderTextureResource& AddStorageTextureOutput(const std::string& name,
	                                               const AttachmentInfo& info,
	                                               const std::string& input = "");
	RenderTextureResource& AddTextureInput(const std::string& name, vk::PipelineStageFlags2 stages = {});
	RenderTextureResource& SetDepthStencilInput(const std::string& name);
	RenderTextureResource& SetDepthStencilOutput(const std::string& name, const AttachmentInfo& info);

	RenderBufferResource& AddIndexBufferInput(const std::string& name);
	RenderBufferResource& AddIndirectBufferInput(const std::string& name);
	RenderBufferResource& AddStorageReadOnlyInput(const std::string& name, vk::PipelineStageFlags2 stages = {});
	RenderBufferResource& AddStorageOutput(const std::string& name,
	                                       const BufferInfo& info,
	                                       const std::string& input = "");
	RenderBufferResource& AddTransferOutput(const std::string& name, const BufferInfo& info);
	RenderBufferResource& AddUniformBufferInput(const std::string& name, vk::PipelineStageFlags2 stages = {});
	RenderBufferResource& AddVertexBufferInput(const std::string& name);

	void AddFakeResourceWriteAlias(const std::string& from, const std::string& to);
	void AddProxyInput(const std::string& name, vk::PipelineStageFlags2 stages);
	void AddProxyOutput(const std::string& name, vk::PipelineStageFlags2 stages);
	void MakeColorInputScaled(uint32_t index);

	bool GetClearColor(uint32_t index, vk::ClearColorValue* value = nullptr) const;
	bool GetClearDepthStencil(vk::ClearDepthStencilValue* value = nullptr) const;
	bool NeedRenderPass() const;
	bool RenderPassIsMultiview() const;
	bool MayNotNeedRenderPass() const;

	void BuildRenderPass(Vulkan::CommandBuffer& cmd, uint32_t layer);
	void PrepareRenderPass(TaskComposer& composer);
	void Setup();
	void SetupDependencies();

	void SetBuildRenderPass(std::function<void(Vulkan::CommandBuffer&)>&& func);
	void SetGetClearColor(std::function<bool(uint32_t, vk::ClearColorValue*)>&& func);
	void SetGetClearDepthStencil(std::function<bool(vk::ClearDepthStencilValue*)>&& func);
	void SetRenderPassInterface(RenderPassInterfaceHandle interface);

	void SetName(const std::string& name);
	void SetPhysicalPassIndex(uint32_t index);

 private:
	RenderBufferResource& AddGenericBufferInput(const std::string& name,
	                                            vk::PipelineStageFlags2 stages,
	                                            vk::AccessFlags2 access,
	                                            vk::BufferUsageFlags usage);

	RenderGraph& _graph;             /** The Render Graph which owns this pass. */
	uint32_t _index;                 /** The index of this Render Pass. */
	std::string _name;               /** The name of this Render Pass. */
	uint32_t _physicalPass = Unused; /** The index of the physical pass this Render Pass belongs to. */
	RenderGraphQueueFlagBits _queue; /** The queue this Render Pass runs on. */

	/** A callback function to record the commands that should run during this Render Pass. */
	std::function<void(Vulkan::CommandBuffer&)> _buildRenderPassFn;
	/** A callback function to determine the clear color for an attachment, if any. */
	std::function<bool(uint32_t, vk::ClearColorValue*)> _getClearColorFn;
	/** A callback function to determine the clear values for the depth/stencil attachment, if any. */
	std::function<bool(vk::ClearDepthStencilValue*)> _getClearDepthStencilFn;
	/** An interface object for managing lambdas. If this is set, it overrides the other callback functions. */
	RenderPassInterfaceHandle _interface;

	std::vector<AccessedBufferResource> _genericBuffers;
	std::vector<AccessedTextureResource> _genericTextures;
	std::vector<AccessedProxyResource> _proxyInputs;
	std::vector<AccessedProxyResource> _proxyOutputs;

	std::vector<RenderBufferResource*> _storageInputs;
	std::vector<RenderBufferResource*> _storageOutputs;
	std::vector<RenderBufferResource*> _transferOutputs;

	std::vector<RenderTextureResource*> _attachmentInputs;
	std::vector<RenderTextureResource*> _blitTextureInputs;
	std::vector<RenderTextureResource*> _blitTextureOutputs;
	std::vector<RenderTextureResource*> _colorInputs;
	std::vector<RenderTextureResource*> _colorOutputs;
	std::vector<RenderTextureResource*> _colorScaleInputs;
	RenderTextureResource* _depthStencilInput  = nullptr;
	RenderTextureResource* _depthStencilOutput = nullptr;
	std::vector<RenderTextureResource*> _historyInputs;
	std::vector<RenderTextureResource*> _resolveOutputs;
	std::vector<RenderTextureResource*> _storageTextureInputs;
	std::vector<RenderTextureResource*> _storageTextureOutputs;

	std::vector<std::pair<RenderTextureResource*, RenderTextureResource*>> _fakeResourceAliases;
};
}  // namespace Luna
