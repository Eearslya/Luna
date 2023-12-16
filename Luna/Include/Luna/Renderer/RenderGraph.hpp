#pragma once

#include <Luna/Core/Threading.hpp>
#include <Luna/Renderer/Common.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

namespace Luna {
/**
 * Represents a Render Graph, describing the process needed to render a full frame.
 * A Render Graph is composed of one or more Render Passes, which each write to resources used by other Render Passes.
 * The job of the Render Graph is to take those Render Passes and all of the resources they read from and write to, and
 * determine how memory access and execution order needs to be organized.
 * The final output of the Render Graph is the swapchain image.
 */
class RenderGraph {
 public:
	RenderGraph();
	RenderGraph(const RenderGraph&)    = delete;
	RenderGraph(RenderGraph&&)         = delete;
	void operator=(const RenderGraph&) = delete;
	void operator=(RenderGraph&&)      = delete;
	~RenderGraph() noexcept;

	void EnqueueRenderPasses(Vulkan::Device& device, TaskComposer& composer);
	void SetupAttachments(Vulkan::Device& device, Vulkan::ImageView* backbuffer);

	void Bake(Vulkan::Device& device);
	void Log();
	void Reset();

	RenderPass& AddPass(const std::string& name, RenderGraphQueueFlagBits queue = RenderGraphQueueFlagBits::Graphics);
	[[nodiscard]] RenderBufferResource& GetBufferResource(const std::string& name);
	[[nodiscard]] Vulkan::Buffer& GetPhysicalBufferResource(const RenderBufferResource& resource);
	[[nodiscard]] Vulkan::Buffer& GetPhysicalBufferResource(uint32_t index);
	[[nodiscard]] ResourceDimensions GetResourceDimensions(const RenderBufferResource& resource) const;
	[[nodiscard]] ResourceDimensions GetResourceDimensions(const RenderTextureResource& resource) const;
	[[nodiscard]] RenderTextureResource& GetTextureResource(const std::string& name);

	void SetBackbufferDimensions(const ResourceDimensions& dimensions);
	void SetBackbufferSource(const std::string& source);

	std::vector<Vulkan::BufferHandle> ConsumePhysicalBuffers() const;
	void InstallPhysicalBuffers(std::vector<Vulkan::BufferHandle>& buffers);

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

	struct Profiling {
		uint32_t StartTimestamp;
		uint32_t EndTimestamp;
		uint64_t TimestampValue;
	};

	struct ScaledClearRequest {
		uint32_t Target;
		uint32_t PhysicalResource;
	};

	struct PassSubmissionState {
		void EmitPrePassBarriers();
		void Submit(Vulkan::Device& device);

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
		uint32_t ArrayLayers = 1;
	};

	struct PipelineEvent {
		vk::PipelineStageFlags2 PipelineBarrierSrcStages = {};
		Vulkan::SemaphoreHandle WaitComputeSemaphore;
		Vulkan::SemaphoreHandle WaitGraphicsSemaphore;
		vk::AccessFlags2 ToFlushAccess          = {};
		vk::AccessFlags2 InvalidatedInStage[64] = {};
		vk::ImageLayout Layout                  = vk::ImageLayout::eUndefined;
	};

	// ===== Render Graph Baking Functions =====
	void ValidatePasses();
	void TraverseDependencies(const RenderPass& pass, uint32_t depth);
	void DependPassesRecursive(const RenderPass& self,
	                           const std::unordered_set<uint32_t>& passes,
	                           uint32_t depth,
	                           bool noCheck,
	                           bool ignoreSelf,
	                           bool mergeDependency);
	void FilterPasses();
	void ReorderPasses();
	void BuildPhysicalResources();
	void BuildPhysicalPasses();
	void BuildTransients();
	void BuildRenderPassInfo();
	void BuildBarriers();
	void BuildPhysicalBarriers();
	void BuildAliases();

	// ===== Render Graph Execution Functions =====
	void EnqueueRenderPass(Vulkan::Device& device,
	                       PhysicalPass& physicalPass,
	                       PassSubmissionState& state,
	                       TaskComposer& composer);
	void EnqueuePhysicalPassCPU(Vulkan::Device& device,
	                            const PhysicalPass& pass,
	                            PassSubmissionState& state,
	                            TaskComposer& composer);
	void EnqueuePhysicalPassGPU(Vulkan::Device& device, const PhysicalPass& physicalPass, PassSubmissionState& state);
	void RecordComputeCommands(const PhysicalPass& physicalPass, PassSubmissionState& state);
	void RecordGraphicsCommands(const PhysicalPass& physicalPass, PassSubmissionState& state);
	void SetupPhysicalBuffer(Vulkan::Device& device, uint32_t physicalIndex);
	void SetupPhysicalImage(Vulkan::Device& device, uint32_t physicalIndex);

	// ===== Render Graph Helper Functions =====
	void GetQueueType(Vulkan::CommandBufferType& cmdType, bool& graphics, RenderGraphQueueFlagBits flag) const;
	void PhysicalPassFlushBarrier(const Barrier& barrier, PassSubmissionState& state);
	void PhysicalPassInvalidateAttachments(const PhysicalPass& physicalPass);
	void PhysicalPassInvalidateBarrier(const Barrier& barrier, PassSubmissionState& state, bool physicalGraphics);
	[[nodiscard]] bool PhysicalPassRequiresWork(const PhysicalPass& physicalPass) const;
	void PhysicalPassSignal(Vulkan::Device& device, const PhysicalPass& physicalPass, PassSubmissionState& state);
	void PhysicalPassTransferOwnership(const PhysicalPass& physicalPass);

	// Backbuffer information
	Vulkan::ImageView* _backbufferAttachment = nullptr;
	ResourceDimensions _backbufferDimensions;
	uint32_t _backbufferPhysicalIndex = RenderResource::Unused;
	std::string _backbufferSource; /** The name of the resource used as the final output. */

	// Render Passes
	std::vector<std::unique_ptr<RenderPass>> _passes;
	std::unordered_map<std::string, uint32_t> _passToIndex;

	// Render Resources
	std::vector<std::unique_ptr<RenderResource>> _resources;
	std::unordered_map<std::string, uint32_t> _resourceToIndex;

	// Baked Render Graph objects
	std::vector<Barriers> _passBarriers;
	std::vector<std::unordered_set<uint32_t>> _passDependencies;
	std::vector<std::unordered_set<uint32_t>> _passMergeDependencies;
	std::vector<uint32_t> _passStack;
	std::vector<uint32_t> _physicalAliases;
	std::vector<Vulkan::ImageView*> _physicalAttachments;
	std::vector<Vulkan::BufferHandle> _physicalBuffers;
	std::vector<ResourceDimensions> _physicalDimensions;
	std::vector<PipelineEvent> _physicalEvents;
	std::vector<PipelineEvent> _physicalHistoryEvents;
	std::vector<Vulkan::ImageHandle> _physicalHistoryImages;
	std::vector<Vulkan::ImageHandle> _physicalImages;
	std::vector<bool> _physicalImageHasHistory;
	std::vector<std::string> _physicalNames;
	std::vector<PhysicalPass> _physicalPasses;
	std::vector<Profiling> _profiling;

	// Render Graph execution state
	std::vector<PassSubmissionState> _passSubmissionStates;

	// Profiling resources
	std::vector<vk::QueryPool> _queryPools;
	uint32_t _startTimestamp = 0;
	uint32_t _endTimestamp   = 0;
};
}  // namespace Luna
