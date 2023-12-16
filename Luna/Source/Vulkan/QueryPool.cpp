#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/QueryPool.hpp>

namespace Luna {
namespace Vulkan {
void QueryResultDeleter::operator()(QueryResult* result) {
	result->_device._queryResultPool.Free(result);
}

QueryResult::QueryResult(Device& device, bool deviceTimebase) : _device(device), _deviceTimebase(deviceTimebase) {}

void QueryResult::SignalTimestampTicks(uint64_t ticks) noexcept {
	_timestampTicks = ticks;
	_hasTimestamp   = true;
}

QueryPool::QueryPool(Device& device) : _device(device) {
	_supportsTimestamp = _device.GetDeviceInfo().Properties.Core.limits.timestampComputeAndGraphics &&
	                     _device.GetDeviceInfo().EnabledFeatures.Vulkan12.hostQueryReset;

	if (_supportsTimestamp) { AddPool(); }
}

QueryPool::~QueryPool() noexcept {
	for (auto& pool : _pools) { _device.GetDevice().destroyQueryPool(pool.Pool); }
}

void QueryPool::Begin() {
	for (uint32_t i = 0; i <= _poolIndex; ++i) {
		if (i >= _pools.size()) { continue; }

		auto& pool = _pools[i];
		if (pool.Index == 0) { continue; }

		const auto queryResult =
			_device.GetDevice().getQueryPoolResults(pool.Pool,
		                                          0,
		                                          pool.Index,
		                                          pool.Index * sizeof(uint64_t),
		                                          pool.Results.data(),
		                                          sizeof(uint64_t),
		                                          vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
		for (uint32_t j = 0; j < pool.Index; ++j) { pool.Cookies[j]->SignalTimestampTicks(pool.Results[j]); }

		_device.GetDevice().resetQueryPool(pool.Pool, 0, pool.Index);
	}

	_poolIndex = 0;
	for (auto& pool : _pools) { pool.Index = 0; }
}

QueryResultHandle QueryPool::WriteTimestamp(vk::CommandBuffer cmd, vk::PipelineStageFlags2 stages) {
	if (!_supportsTimestamp) { return {}; }

	if (_pools[_poolIndex].Index >= _pools[_poolIndex].Size) { _poolIndex++; }
	if (_poolIndex >= _pools.size()) { AddPool(); }

	auto& pool               = _pools[_poolIndex];
	auto cookie              = QueryResultHandle(_device._queryResultPool.Allocate(_device, true));
	pool.Cookies[pool.Index] = cookie;

	cmd.writeTimestamp2(stages, pool.Pool, pool.Index);
	pool.Index++;

	return cookie;
}

void QueryPool::AddPool() {
	const vk::QueryPoolCreateInfo poolCI({}, vk::QueryType::eTimestamp, 64);
	auto queryPool = _device.GetDevice().createQueryPool(poolCI);
	_device.GetDevice().resetQueryPool(queryPool, 0, poolCI.queryCount);

	auto& pool = _pools.emplace_back();
	pool.Pool  = queryPool;
	pool.Size  = poolCI.queryCount;
	pool.Index = 0;
	pool.Results.resize(pool.Size);
	pool.Cookies.resize(pool.Size);
}

TimestampInterval::TimestampInterval(const std::string& name) : _name(name) {}

double TimestampInterval::GetTimePerAccumulation() const noexcept {
	if (_totalAccumulations) { return _totalTime / double(_totalAccumulations); }

	return 0.0;
}

void TimestampInterval::AccumulateTime(double t) noexcept {
	_totalTime += t;
	_totalAccumulations++;
}

void TimestampInterval::Reset() noexcept {
	_totalTime          = 0.0;
	_totalAccumulations = 0;
}
}  // namespace Vulkan
}  // namespace Luna
