#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <unordered_map>

namespace Luna {
/**
 * Represents a Render Graph, describing the process needed to render a full frame.
 * A Render Graph is composed of one or more Render Passes, which each write to resources used by other Render Passes.
 * The job of the Render Graph is to take those Render Passes and all of the resources they read to and write from, and
 * determine how memory access and execution order needs to be organized.
 * The final output of the Render Graph is the swapchain image.
 */
class RenderGraph {
 public:
	/**
	 * Initializes a new RenderGraph object.
	 */
	RenderGraph();

	// Do not allow copies or moves.
	RenderGraph(const RenderGraph&)    = delete;
	RenderGraph(RenderGraph&&)         = delete;
	void operator=(const RenderGraph&) = delete;
	void operator=(RenderGraph&&)      = delete;

	/** Cleans up the RenderGraph object. */
	~RenderGraph() noexcept;

	/**
	 * Finalizes the Render Graph into an executable state.
	 * Must be called before EnqueueRenderPasses can be used.
	 */
	void Bake();

	/**
	 * Enqueue the baked render passes into the given task composer.
	 *
	 * @param composer The Task Composer object to enqueue all of the necessary tasks to.
	 */
	void EnqueueRenderPasses(Vulkan::Device& device, TaskComposer& composer);

	/** Prints debug information about the Render Pass to the logs. */
	void Log();

	/** Resets the Render Graph and releases any created resources. */
	void Reset();

	void SetupAttachments(Vulkan::ImageView* swapchain);

	/**
	 * Adds a new Render Pass to the graph.
	 *
	 * @param name The name of the Render Pass.
	 * @param queue The Queue which will run the Render Pass.
	 *
	 * @return The created Render Pass object, or an existing one if the name is already in use.
	 */
	RenderPass& AddPass(const std::string& name, RenderGraphQueueFlagBits queue);

	/**
	 * Finds a Render Pass by the given name.
	 *
	 * @param name The name of the Render Pass.
	 *
	 * @return A pointer to the found Render Pass, or nullptr if none could be found.
	 */
	RenderPass* FindPass(const std::string& name);
	std::vector<Vulkan::BufferHandle> ConsumePhysicalBuffers() const;
	RenderBufferResource& GetBufferResource(const std::string& name);
	Vulkan::Buffer& GetPhysicalBufferResource(const RenderBufferResource& resource);
	Vulkan::Buffer& GetPhysicalBufferResource(uint32_t index);
	Vulkan::ImageView& GetPhysicalTextureResource(const RenderTextureResource& resource);
	Vulkan::ImageView& GetPhysicalTextureResource(uint32_t index);
	RenderResource& GetProxyResource(const std::string& name);
	ResourceDimensions GetResourceDimensions(const RenderBufferResource& resource) const;
	ResourceDimensions GetResourceDimensions(const RenderTextureResource& resource) const;
	RenderTextureResource& GetTextureResource(const std::string& name);
	void InstallPhysicalBuffers(std::vector<Vulkan::BufferHandle>& buffers);
	void SetBackbufferSource(const std::string& name);
	void SetBackbufferDimensions(const ResourceDimensions& dim);
	RenderTextureResource* TryGetTextureResource(const std::string& name);

 private:
	struct Barrier {
		uint32_t ResourceIndex;
		vk::ImageLayout Layout;
		vk::AccessFlags2 Access;
		vk::PipelineStageFlags2 Stages;
		bool History;
	};

	struct Barriers {
		std::vector<Barrier> Invalidate;
		std::vector<Barrier> Flush;
	};

	struct ColorClearRequest {
		RenderPass* Pass;
		vk::ClearColorValue* Target;
		uint32_t Index;
	};

	struct DepthClearRequest {
		RenderPass* Pass;
		vk::ClearDepthStencilValue* Target;
	};

	struct MipmapRequest {
		uint32_t PhysicalResource;
		vk::PipelineStageFlags2 Stages;
		vk::AccessFlags2 Access;
		vk::ImageLayout Layout;
	};

	struct ScaledClearRequest {
		uint32_t Target;
		uint32_t PhysicalResource;
	};

	struct PassSubmissionState {
		void EmitPrePassBarriers();
		void Submit();

		std::vector<vk::BufferMemoryBarrier2> BufferBarriers;
		std::vector<vk::ImageMemoryBarrier2> ImageBarriers;
		std::vector<vk::SubpassContents> SubpassContents;
		std::vector<Vulkan::SemaphoreHandle> WaitSemaphores;
		std::vector<vk::PipelineStageFlags2> WaitStages;

		Vulkan::SemaphoreHandle ProxySemaphores[2];
		bool NeedSubmissionSemaphore = false;

		Vulkan::CommandBufferHandle Cmd;

		Vulkan::CommandBufferType QueueType;
		bool Active   = false;
		bool Graphics = false;

		TaskGroupHandle RenderingDependency;
	};

	struct PhysicalPass {
		std::vector<uint32_t> Passes;
		std::vector<uint32_t> Discards;
		std::vector<Barrier> Invalidate;
		std::vector<Barrier> Flush;
		std::vector<Barrier> History;
		std::vector<std::pair<uint32_t, uint32_t>> AliasTransfer;

		Vulkan::RenderPassInfo RenderPassInfo;
		std::vector<uint32_t> PhysicalColorAttachments;
		uint32_t PhysicalDepthStencilAttachment = RenderResource::Unused;

		std::vector<ColorClearRequest> ColorClearRequests;
		DepthClearRequest DepthClearRequest;

		std::vector<std::vector<ScaledClearRequest>> ScaledClearRequests;
		std::vector<MipmapRequest> MipmapRequests;
		uint32_t Layers = 1;
	};

	struct PipelineEvent {
		vk::PipelineStageFlags2 PipelineBarrierSrcStages = {};
		Vulkan::SemaphoreHandle WaitComputeSemaphore;
		Vulkan::SemaphoreHandle WaitGraphicsSemaphore;
		vk::AccessFlags2 ToFlushAccess          = {};
		vk::AccessFlags2 InvalidatedInStage[64] = {};
		vk::ImageLayout Layout                  = vk::ImageLayout::eUndefined;
	};

	void BuildAliases();
	void BuildBarriers();
	void BuildPhysicalBarriers();
	void BuildPhysicalPasses();
	void BuildPhysicalResources();
	void BuildRenderPassInfo();
	void BuildTransients();
	void DependPassesRecursive(const RenderPass& pass,
	                           const std::unordered_set<uint32_t>& passes,
	                           uint32_t depth,
	                           bool noCheck,
	                           bool ignoreSelf,
	                           bool mergeDependency);
	void EnqueueRenderPass(Vulkan::Device& device,
	                       PhysicalPass& physicalPass,
	                       PassSubmissionState& state,
	                       TaskComposer& composer);
	void FilterPasses();
	void GetQueueType(Vulkan::CommandBufferType& queueType, bool& graphics, RenderGraphQueueFlagBits flag) const;
	bool NeedsInvalidate(const Barrier& barrier, const PipelineEvent& evnet) const;
	void PerformScaleRequests(Vulkan::CommandBuffer& cmd, const std::vector<ScaledClearRequest>& requests);
	void PhysicalPassEnqueueComputeCommands(const PhysicalPass& physicalPass, PassSubmissionState& state);
	void PhysicalPassEnqueueGraphicsCommands(const PhysicalPass& physicalPass, PassSubmissionState& state);
	void PhysicalPassHandleCPU(Vulkan::Device& device,
	                           const PhysicalPass& pass,
	                           PassSubmissionState& state,
	                           TaskComposer& composer);
	void PhysicalPassHandleFlushBarrier(const Barrier& barrier, PassSubmissionState& state);
	void PhysicalPassHandleGPU(Vulkan::Device& device, const PhysicalPass& pass, PassSubmissionState& state);
	void PhysicalPassHandleSignal(Vulkan::Device& device, const PhysicalPass& physicalPass, PassSubmissionState& state);
	void PhysicalPassInvalidateAttachments(const PhysicalPass& physicalPass);
	void PhysicalPassInvalidateBarrier(const Barrier& barrier, PassSubmissionState& state, bool physicalGraphics);
	bool PhysicalPassRequiresWork(const PhysicalPass& physicalPass) const;
	void PhysicalPassTransferOwnership(const PhysicalPass& physicalPass);
	void ReorderPasses();
	void SetupPhysicalBuffer(uint32_t attachment);
	void SetupPhysicalImage(uint32_t attachment);
	void SwapchainScalePass();
	void TraverseDependencies(const RenderPass& pass, uint32_t depth);
	void ValidatePasses();

	/** The name of the resource used as the final output. */
	std::string _backbufferSource;
	/** An array of all Render Pass objects. */
	std::vector<std::unique_ptr<RenderPass>> _passes;
	std::vector<Barriers> _passBarriers;
	std::vector<std::unordered_set<uint32_t>> _passDependencies;
	std::vector<std::unordered_set<uint32_t>> _passMergeDependencies;
	std::vector<uint32_t> _passStack;
	std::vector<PassSubmissionState> _passSubmissionStates;
	/** A map to associate a Render Pass name with its index in _passes. */
	std::unordered_map<std::string, uint32_t> _passToIndex;
	/** An array of all the resources in the graph. */
	std::vector<std::unique_ptr<RenderResource>> _resources;
	/** A map to associate a Resource name with its index in _resources. */
	std::unordered_map<std::string, uint32_t> _resourceToIndex;
	/** The swapchain attachment we're outputting to this frame. */
	Vulkan::ImageView* _swapchainAttachment = nullptr;
	/** The dimensions of the swapchain this frame. */
	ResourceDimensions _swapchainDimensions;
	/** The physical index of the resource aliasing the swapchain, if any. */
	uint32_t _swapchainPhysicalIndex = RenderResource::Unused;

	std::vector<uint32_t> _physicalAliases;
	std::vector<Vulkan::ImageView*> _physicalAttachments;
	std::vector<Vulkan::BufferHandle> _physicalBuffers;
	std::vector<ResourceDimensions> _physicalDimensions;
	std::vector<PipelineEvent> _physicalEvents;
	std::vector<PipelineEvent> _physicalHistoryEvents;
	std::vector<Vulkan::ImageHandle> _physicalImageAttachments;
	std::vector<bool> _physicalImageHasHistory;
	std::vector<Vulkan::ImageHandle> _physicalHistoryImageAttachments;
	std::vector<PhysicalPass> _physicalPasses;
};
}  // namespace Luna
