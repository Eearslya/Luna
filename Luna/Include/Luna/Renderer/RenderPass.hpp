#pragma once

#include <Luna/Core/Threading.hpp>
#include <Luna/Renderer/Common.hpp>

namespace Luna {
/**
 * A helper class used to manage all of the functions needed to control a render pass.
 * More convenient than throwing lambdas all over the place.
 */
class RenderPassInterface : public IntrusivePtrEnabled<RenderPassInterface> {
 public:
	virtual ~RenderPassInterface() = default;

	/**
	 * Determines whether or not this Render Pass may be disabled or enabled each frame.
	 * This function must always return the same result for a given Interface.
	 */
	virtual bool RenderPassIsConditional() const;

	/**
	 * Determines whether this Render Pass has separate layers or not.
	 * This function must always return the same result for a given Interface.
	 */
	virtual bool RenderPassIsSeparateLayered() const;

	/**
	 * Retrieves the color clear value for a given attachment.
	 * Returns false if the attachment should not be cleared.
	 */
	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const;

	/**
	 * Retrieves the depth/stencil clear value.
	 * Returns false if the attachment should not be cleared.
	 */
	virtual bool GetClearDepthStencil(vk::ClearDepthStencilValue* value) const;

	/**
	 * Determine whether or not this Render Pass should run this frame.
	 * This function is only used when RenderPassIsConditional() returns true.
	 */
	virtual bool NeedRenderPass() const;

	/**
	 * Set up any necessary dependencies.
	 * This function is called once before the RenderGraph is baked.
	 */
	virtual void SetupDependencies(RenderPass& pass, RenderGraph& graph);

	/**
	 * Perform any needed post setup.
	 * This function is called once after the Render Graph is baked.
	 */
	virtual void Setup();

	/**
	 * Records the Vulkan command that should run during this Render Pass.
	 * All commands will be executed within a subpass.
	 */
	virtual void BuildRenderPass(Vulkan::CommandBuffer& cmd);

	/**
	 * Records the Vulkan command that should run during this Render Pass, for the specified layer.
	 * All commands will be executed within a subpass.
	 */
	virtual void BuildRenderPassSeparateLayer(Vulkan::CommandBuffer& cmd, uint32_t layer);

	/**
	 * Called every frame to allow the Render Pass a chance to prepare dependent resources.
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
	struct AccessedBufferResource : AccessedResource {
		RenderBufferResource* Buffer = nullptr;
	};
	struct AccessedProxyResource : AccessedResource {
		RenderResource* Proxy = nullptr;
	};
	struct AccessedTextureResource : AccessedResource {
		RenderTextureResource* Texture = nullptr;
	};

	RenderPass(RenderGraph& graph, uint32_t index, RenderGraphQueueFlagBits queue);

	[[nodiscard]] RenderGraph& GetGraph() noexcept {
		return _graph;
	}
	[[nodiscard]] uint32_t GetIndex() const noexcept {
		return _index;
	}
	[[nodiscard]] const std::string& GetName() const noexcept {
		return _name;
	}
	[[nodiscard]] uint32_t GetPhysicalPassIndex() const noexcept {
		return _physicalPass;
	}
	[[nodiscard]] RenderGraphQueueFlagBits GetQueue() const noexcept {
		return _queue;
	}

	[[nodiscard]] const std::vector<AccessedBufferResource>& GetGenericBufferInputs() const noexcept {
		return _genericBuffers;
	}
	[[nodiscard]] const std::vector<AccessedTextureResource>& GetGenericTextureInputs() const noexcept {
		return _genericTextures;
	}
	[[nodiscard]] const std::vector<AccessedProxyResource>& GetProxyInputs() const noexcept {
		return _proxyInputs;
	}
	[[nodiscard]] const std::vector<AccessedProxyResource>& GetProxyOutputs() const noexcept {
		return _proxyOutputs;
	}

	[[nodiscard]] const std::vector<RenderBufferResource*>& GetStorageInputs() const noexcept {
		return _storageInputs;
	}
	[[nodiscard]] const std::vector<RenderBufferResource*>& GetStorageOutputs() const noexcept {
		return _storageOutputs;
	}
	[[nodiscard]] const std::vector<RenderBufferResource*>& GetTransferOutputs() const noexcept {
		return _transferOutputs;
	}

	[[nodiscard]] const std::vector<RenderTextureResource*>& GetAttachmentInputs() const noexcept {
		return _attachmentInputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetBlitTextureInputs() const noexcept {
		return _blitTextureInputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetBlitTextureOutputs() const noexcept {
		return _blitTextureOutputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetColorInputs() const noexcept {
		return _colorInputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetColorOutputs() const noexcept {
		return _colorOutputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetColorScaleInputs() const noexcept {
		return _colorScaleInputs;
	}
	[[nodiscard]] RenderTextureResource* GetDepthStencilInput() const noexcept {
		return _depthStencilInput;
	}
	[[nodiscard]] RenderTextureResource* GetDepthStencilOutput() const noexcept {
		return _depthStencilOutput;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetHistoryInputs() const noexcept {
		return _historyInputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetResolveOutputs() const noexcept {
		return _resolveOutputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetStorageTextureInputs() const noexcept {
		return _storageTextureInputs;
	}
	[[nodiscard]] const std::vector<RenderTextureResource*>& GetStorageTextureOutputs() const noexcept {
		return _storageTextureOutputs;
	}

	[[nodiscard]] const std::vector<std::pair<RenderTextureResource*, RenderTextureResource*>>& GetFakeResourceAliases()
		const noexcept {
		return _fakeResourceAliases;
	}

	RenderTextureResource& AddTextureInput(const std::string& name, vk::PipelineStageFlags2 stages = {});

	RenderTextureResource& AddColorOutput(const std::string& name,
	                                      const AttachmentInfo& info,
	                                      const std::string& input = "");
	RenderTextureResource& SetDepthStencilOutput(const std::string& name, const AttachmentInfo& info);

	void MakeColorInputScaled(uint32_t index);

	bool GetClearColor(uint32_t index, vk::ClearColorValue* value = nullptr) const;
	bool GetClearDepthStencil(vk::ClearDepthStencilValue* value = nullptr) const;
	bool NeedRenderPass() const;
	bool RenderPassIsConditional() const;
	bool RenderPassIsMultiview() const;

	void BuildRenderPass(Vulkan::CommandBuffer& cmd, uint32_t layer);
	void PrepareRenderPass(TaskComposer& composer);
	void Setup();
	void SetupDependencies();

	void SetBuildRenderPass(std::function<void(Vulkan::CommandBuffer&)>&& func) noexcept;
	void SetGetClearColor(std::function<bool(uint32_t, vk::ClearColorValue*)>&& func) noexcept;
	void SetGetClearDepthStencil(std::function<bool(vk::ClearDepthStencilValue*)>&& func) noexcept;
	void SetRenderPassInterface(RenderPassInterfaceHandle interface) noexcept;

	void SetName(const std::string& name) noexcept;
	void SetPhysicalPassIndex(uint32_t index) noexcept;

 private:
	RenderGraph& _graph;             /** The RenderGraph which owns this pass. */
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
	/** An interface object for managing callbacks. If this is set, it overrides the other callback functions. */
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
