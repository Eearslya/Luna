#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct QueryResultDeleter {
	void operator()(QueryResult* result);
};

class QueryResult : public VulkanObject<QueryResult, QueryResultDeleter> {
	friend class ObjectPool<QueryResult>;
	friend struct QueryResultDeleter;

 public:
	[[nodiscard]] uint64_t GetTimestampTicks() const noexcept {
		return _timestampTicks;
	}
	[[nodiscard]] bool IsDeviceTimebase() const noexcept {
		return _deviceTimebase;
	}
	[[nodiscard]] bool IsSignalled() const noexcept {
		return _hasTimestamp;
	}

	void SignalTimestampTicks(uint64_t ticks) noexcept;

 private:
	QueryResult(Device& device, bool deviceTimebase);

	Device& _device;
	uint64_t _timestampTicks = 0;
	bool _hasTimestamp       = false;
	bool _deviceTimebase     = false;
};

class QueryPool {
 public:
	QueryPool(Device& device);
	~QueryPool() noexcept;

	void Begin();
	QueryResultHandle WriteTimestamp(vk::CommandBuffer cmd, vk::PipelineStageFlags2 stages);

 private:
	struct Pool {
		vk::QueryPool Pool;
		std::vector<uint64_t> Results;
		std::vector<QueryResultHandle> Cookies;
		uint32_t Index = 0;
		uint32_t Size  = 0;
	};

	void AddPool();

	Device& _device;
	std::vector<Pool> _pools;
	uint32_t _poolIndex     = 0;
	bool _supportsTimestamp = false;
};

class TimestampInterval : public IntrusiveHashMapEnabled<TimestampInterval> {
 public:
	TimestampInterval(const std::string& name);

	[[nodiscard]] const std::string& GetName() const noexcept {
		return _name;
	}
	[[nodiscard]] uint64_t GetTotalAccumulations() const noexcept {
		return _totalAccumulations;
	}
	[[nodiscard]] double GetTotalTime() const noexcept {
		return _totalTime;
	}

	[[nodiscard]] double GetTimePerAccumulation() const noexcept;

	void AccumulateTime(double t) noexcept;
	void Reset() noexcept;

 private:
	std::string _name;
	double _totalTime            = 0.0;
	uint64_t _totalAccumulations = 0;
};
}  // namespace Vulkan
}  // namespace Luna
