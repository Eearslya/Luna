#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Utility/RadixSorter.hpp>

namespace Luna {
using RenderFunc = void (*)(Vulkan::CommandBuffer&, const RenderQueueData*, uint32_t);

struct RenderQueueData {
	RenderFunc Render;
	const void* RenderInfo;
	const void* InstanceData;
	uint64_t SortingKey;
};

struct RenderQueueDataVector {
	void Clear() {
		RawInput.clear();
	}
	size_t Size() const {
		return RawInput.size();
	}
	const RenderQueueData* SortedData() const {
		return SortedOutput.data();
	}

	std::vector<RenderQueueData> RawInput;
	std::vector<RenderQueueData> SortedOutput;
	RadixSorter<uint64_t, 8, 8, 8, 8, 8, 8, 8, 8> Sorter;
};

struct RenderQueueDataWrappedErased : IntrusiveHashMapEnabled<RenderQueueDataWrappedErased> {};

template <typename T>
struct RenderQueueDataWrapped : RenderQueueDataWrappedErased {
	T Data;
};

class RenderQueue {
 public:
	constexpr static const size_t BlockSize = 64 * 1024;

	RenderQueue();
	RenderQueue(const RenderQueue&)    = delete;
	void operator=(const RenderQueue&) = delete;
	~RenderQueue() noexcept;

	const RenderQueueDataVector& GetQueueData(RenderQueueType type) const {
		return _queues[int(type)];
	}
	ShaderSuite* GetShaderSuites() const {
		return _shaderSuites;
	}

	void* Allocate(size_t size, size_t alignment = 64);
	void Dispatch(RenderQueueType type, Vulkan::CommandBuffer& cmd, const Vulkan::CommandBufferSavedState& state) const;
	void DispatchRange(RenderQueueType type,
	                   Vulkan::CommandBuffer& cmd,
	                   const Vulkan::CommandBufferSavedState& state,
	                   size_t begin,
	                   size_t end) const;
	void DispatchSubset(RenderQueueType type,
	                    Vulkan::CommandBuffer& cmd,
	                    const Vulkan::CommandBufferSavedState& state,
	                    uint32_t subsetIndex,
	                    uint32_t subsetCount) const;
	void PushRenderables(const RenderContext& context, const VisibilityList& renderables);
	void PushDepthRenderables(const RenderContext& context, const VisibilityList& renderables);
	void Reset();
	void SetShaderSuites(ShaderSuite* suites);
	void Sort();

	template <typename T>
	T* AllocateOne() {
		static_assert(std::is_trivially_destructible_v<T>, "RenderQueue data must be trivially destructible.");
		auto* data = static_cast<T*>(Allocate(sizeof(T), alignof(T)));
		if (data) { new (data) T(); }
		return data;
	}

	template <typename T>
	T* AllocateMany(size_t count) {
		static_assert(std::is_trivially_destructible_v<T>, "RenderQueue data must be trivially destructible.");
		auto* data = static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
		if (data) {
			for (size_t i = 0; i < count; ++i) { new (&data[i]) T(); }
		}
		return data;
	}

	template <typename T>
	T* Push(RenderQueueType type, Hash instanceKey, uint64_t sortingKey, RenderFunc callback, void* instanceData) {
		static_assert(std::is_trivially_destructible_v<T>, "RenderQueue data must be trivially destructible.");
		using WrappedT = RenderQueueDataWrapped<T>;

		Hasher h(instanceKey);
		h(callback);
		const auto hash = h.Get();

		auto* it = _renderInfos.Find(hash);
		if (it) {
			auto* t = static_cast<WrappedT*>(it);
			EnqueueQueueData(type, {callback, &t->Data, instanceData, sortingKey});

			return nullptr;
		} else {
			void* buffer = AllocateOne<WrappedT>();
			if (!buffer) { throw std::bad_alloc(); }

			auto* t = new (buffer) WrappedT();
			t->SetHash(hash);
			_renderInfos.InsertReplace(t);
			EnqueueQueueData(type, {callback, &t->Data, instanceData, sortingKey});

			return &t->Data;
		}
	}

 private:
	struct Block : IntrusivePtrEnabled<Block> {
		Block() {
			Begin = reinterpret_cast<uintptr_t>(InlineBuffer);
			End   = Begin + BlockSize;
			Reset();
		}
		Block(size_t size) {
			LargeBuffer.reset(new uint8_t[size]);
			Begin = reinterpret_cast<uintptr_t>(LargeBuffer.get());
			End   = Begin + size;
			Reset();
		}
		Block(const Block&)          = delete;
		void operator=(const Block&) = delete;

		void Reset() {
			Pointer = Begin;
		}

		constexpr static size_t BlockSize = 64 * 1024;

		std::unique_ptr<uint8_t[]> LargeBuffer;
		uintptr_t Pointer = 0;
		uintptr_t Begin   = 0;
		uintptr_t End     = 0;
		uint8_t InlineBuffer[BlockSize];
	};

	static ThreadSafeObjectPool<Block> _allocatorPool;
	static void* AllocateFromBlock(Block& block, size_t size, size_t alignment);

	void EnqueueQueueData(RenderQueueType type, const RenderQueueData& data);
	Block* InsertBlock();
	Block* InsertLargeBlock(size_t size, size_t alignment);
	void RecycleBlocks();

	std::vector<Block*> _blocks;
	Block* _currentBlock = nullptr;
	std::array<RenderQueueDataVector, RenderQueueTypeCount> _queues;
	IntrusiveHashMapHolder<RenderQueueDataWrappedErased> _renderInfos;
	ShaderSuite* _shaderSuites = nullptr;
};
}  // namespace Luna
