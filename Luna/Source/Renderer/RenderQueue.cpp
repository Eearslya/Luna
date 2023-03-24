#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/Renderable.hpp>

namespace Luna {
ThreadSafeObjectPool<RenderQueue::Block> RenderQueue::_allocatorPool;

RenderQueue::RenderQueue() {}

RenderQueue::~RenderQueue() noexcept {
	RecycleBlocks();
}

void* RenderQueue::Allocate(size_t size, size_t alignment) {
	if (size + alignment > BlockSize) {
		auto* block = InsertLargeBlock(size, alignment);
		return AllocateFromBlock(*block, size, alignment);
	}

	if (!_currentBlock) { _currentBlock = InsertBlock(); }

	void* data = AllocateFromBlock(*_currentBlock, size, alignment);
	if (data) { return data; }

	_currentBlock = InsertBlock();
	data          = AllocateFromBlock(*_currentBlock, size, alignment);

	return data;
}

void RenderQueue::Dispatch(RenderQueueType type, Vulkan::CommandBuffer& cmd) const {
	DispatchRange(type, cmd, 0, _queues[int(type)].Size());
}

void RenderQueue::DispatchRange(RenderQueueType type, Vulkan::CommandBuffer& cmd, size_t begin, size_t end) const {
	auto* queue = _queues[int(type)].SortedData();

	while (begin < end) {
		uint32_t instances = 1;
		for (size_t i = begin + 1; i < end && queue[i].RenderInfo == queue[begin].RenderInfo; ++i) { instances++; }

		queue[begin].Render(cmd, &queue[begin], instances);
		begin += instances;
	}
}

void RenderQueue::DispatchSubset(RenderQueueType type,
                                 Vulkan::CommandBuffer& cmd,
                                 uint32_t subsetIndex,
                                 uint32_t subsetCount) const {
	const size_t size       = _queues[int(type)].Size();
	const size_t beginIndex = (size * subsetIndex) / subsetCount;
	const size_t endIndex   = (size * (subsetIndex + 1)) / subsetCount;
	DispatchRange(type, cmd, beginIndex, endIndex);
}

void RenderQueue::PushRenderables(const RenderContext& context, const VisibilityList& renderables) {
	for (const auto& renderable : renderables) { renderable.Handle->Enqueue(context, renderable, *this); }
}

void RenderQueue::Reset() {
	RecycleBlocks();
	for (auto& queue : _queues) { queue.Clear(); }
	_renderInfos.Clear();
}

void RenderQueue::SetShaderSuites(ShaderSuite* suites) {
	_shaderSuites = suites;
}

void RenderQueue::Sort() {
	for (auto& queue : _queues) {
		queue.Sorter.Resize(queue.RawInput.size());
		queue.SortedOutput.resize(queue.RawInput.size());

		size_t n                = queue.RawInput.size();
		uint64_t* codes         = queue.Sorter.CodeData();
		const uint32_t* indices = queue.Sorter.IndicesData();

		for (size_t i = 0; i < n; ++i) { codes[i] = queue.RawInput[i].SortingKey; }
		queue.Sorter.Sort();
		for (size_t i = 0; i < n; ++i) { queue.SortedOutput[i] = queue.RawInput[indices[i]]; }
	}
}

void* RenderQueue::AllocateFromBlock(Block& block, size_t size, size_t alignment) {
	block.Pointer = (block.Pointer + alignment - 1) & ~(alignment - 1);
	uintptr_t end = block.Pointer + size;
	if (end <= block.End) {
		void* ret     = reinterpret_cast<void*>(block.Pointer);
		block.Pointer = end;

		return ret;
	}

	return nullptr;
}

void RenderQueue::EnqueueQueueData(RenderQueueType type, const RenderQueueData& data) {
	_queues[int(type)].RawInput.push_back(data);
}

RenderQueue::Block* RenderQueue::InsertBlock() {
	auto* ret = _allocatorPool.Allocate();
	_blocks.push_back(ret);

	return ret;
}

RenderQueue::Block* RenderQueue::InsertLargeBlock(size_t size, size_t alignment) {
	size_t paddedSize = alignment > alignof(uintmax_t) ? (size + alignment) : size;
	auto* ret         = _allocatorPool.Allocate(paddedSize);
	_blocks.push_back(ret);

	return ret;
}

void RenderQueue::RecycleBlocks() {
	for (auto* block : _blocks) { _allocatorPool.Free(block); }
	_blocks.clear();
	_currentBlock = nullptr;
}
}  // namespace Luna
