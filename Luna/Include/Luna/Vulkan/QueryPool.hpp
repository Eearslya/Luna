#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class PerformanceQueryPool : public IntrusivePtrEnabled<PerformanceQueryPool> {
 public:
	PerformanceQueryPool(Device& device, uint32_t queueFamily);
	~PerformanceQueryPool() noexcept;

	const std::vector<vk::PerformanceCounterKHR>& GetCounters() const {
		return _counters;
	}
	const std::vector<vk::PerformanceCounterDescriptionKHR>& GetDescriptions() const {
		return _counterDescriptions;
	}

	bool InitCounters(const std::vector<std::string>& enableCounterNames);
	void BeginCommandBuffer(vk::CommandBuffer cmd);
	void EndCommandBuffer(vk::CommandBuffer cmd);
	void Report();

	static void LogCounters(const std::vector<vk::PerformanceCounterKHR>& counters,
	                        const std::vector<vk::PerformanceCounterDescriptionKHR>& descriptions);

 private:
	Device& _device;
	uint32_t _queueFamily = 0;
	vk::QueryPool _queryPool;

	std::vector<uint32_t> _activeIndices;
	std::vector<vk::PerformanceCounterKHR> _counters;
	std::vector<vk::PerformanceCounterDescriptionKHR> _counterDescriptions;
	std::vector<vk::PerformanceCounterResultKHR> _results;
};

struct QueryPoolResultDeleter {
	void operator()(QueryPoolResult* result);
};

class QueryPoolResult : public IntrusivePtrEnabled<QueryPoolResult, QueryPoolResultDeleter, HandleCounter> {
	friend class ObjectPool<QueryPoolResult>;
	friend struct QueryPoolResultDeleter;

 public:
	uint64_t GetTimestampTicks() const {
		return _timestampTicks;
	}
	bool IsDeviceTimebase() const {
		return _deviceTimebase;
	}
	bool IsSignalled() const {
		return _hasTimestamp;
	}

	void SignalTimestampTicks(uint64_t ticks);

 private:
	explicit QueryPoolResult(Device& device, bool deviceTimebase);

	Device& _device;
	uint64_t _timestampTicks = 0;
	bool _deviceTimebase     = false;
	bool _hasTimestamp       = false;
};

class QueryPool {
 public:
	explicit QueryPool(Device& device);
	~QueryPool() noexcept;

	void Begin();
	QueryPoolResultHandle WriteTimestamp(vk::CommandBuffer cmd, vk::PipelineStageFlagBits stage);

 private:
	struct Pool {
		vk::QueryPool Pool;
		std::vector<uint64_t> QueryResults;
		std::vector<QueryPoolResultHandle> Cookies;
		uint32_t Index = 0;
		uint32_t Size  = 0;
	};

	void AddPool();

	Device& _device;
	std::vector<Pool> _pools;
	uint32_t _poolIndex     = 0;
	bool _supportsTimestamp = false;
};
}  // namespace Vulkan
}  // namespace Luna
