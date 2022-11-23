#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/QueryPool.hpp>

namespace Luna {
namespace Vulkan {
const char* UnitToString(vk::PerformanceCounterUnitKHR unit) {
	switch (unit) {
		case vk::PerformanceCounterUnitKHR::eAmps:
			return "A";
		case vk::PerformanceCounterUnitKHR::eBytes:
			return "B";
		case vk::PerformanceCounterUnitKHR::eBytesPerSecond:
			return "B/s";
		case vk::PerformanceCounterUnitKHR::eCycles:
			return "Cycles";
		case vk::PerformanceCounterUnitKHR::eGeneric:
			return "";
		case vk::PerformanceCounterUnitKHR::eHertz:
			return "Hz";
		case vk::PerformanceCounterUnitKHR::eKelvin:
			return "K";
		case vk::PerformanceCounterUnitKHR::eNanoseconds:
			return "ns";
		case vk::PerformanceCounterUnitKHR::ePercentage:
			return "%";
		case vk::PerformanceCounterUnitKHR::eVolts:
			return "V";
		case vk::PerformanceCounterUnitKHR::eWatts:
			return "W";
		default:
			return "Unknown";
	}
}

PerformanceQueryPool::PerformanceQueryPool(Device& device, uint32_t queueFamily)
		: _device(device), _queueFamily(queueFamily) {
	if (!_device.GetDeviceInfo().EnabledFeatures.PerformanceQuery.performanceCounterQueryPools) { return; }

	auto physDev = _device.GetDeviceInfo().PhysicalDevice;
	auto dev     = _device.GetDevice();

	const auto counters  = physDev.enumerateQueueFamilyPerformanceQueryCountersKHR(_queueFamily);
	_counters            = counters.first;
	_counterDescriptions = counters.second;
}

PerformanceQueryPool::~PerformanceQueryPool() noexcept {
	if (_queryPool) {
		auto dev = _device.GetDevice();
		dev.destroyQueryPool(_queryPool);
	}
}

bool PerformanceQueryPool::InitCounters(const std::vector<std::string>& enableCounterNames) {
	if (!_device.GetDeviceInfo().EnabledFeatures.PerformanceQuery.performanceCounterQueryPools) { return false; }
	if (!_device.GetDeviceInfo().EnabledFeatures.Vulkan12.hostQueryReset) { return false; }

	auto physDev = _device.GetDeviceInfo().PhysicalDevice;
	auto dev     = _device.GetDevice();

	if (_queryPool) { dev.destroyQueryPool(_queryPool); }
	_queryPool = nullptr;

	_activeIndices.clear();
	for (auto& name : enableCounterNames) {
		auto it = std::find_if(
			std::begin(_counterDescriptions),
			std::end(_counterDescriptions),
			[&](const vk::PerformanceCounterDescriptionKHR& desc) { return strcmp(desc.name.data(), name.c_str()) == 0; });

		if (it != std::end(_counterDescriptions)) {
			Log::Debug("Vulkan-Performance", "Found counter {}: {}", it->name.data(), it->description.data());
			_activeIndices.push_back(it - std::begin(_counterDescriptions));
		}
	}
	if (_activeIndices.empty()) {
		Log::Warning("Vulkan-Performance", "No performance counters were enabled.");
		return false;
	}

	_results.resize(_activeIndices.size());
	const vk::QueryPoolPerformanceCreateInfoKHR performanceCI(_queueFamily, _activeIndices);
	const vk::QueryPoolCreateInfo poolCI({}, vk::QueryType::ePerformanceQueryKHR, 1, {});
	const vk::StructureChain chain(poolCI, performanceCI);

	uint32_t passes = physDev.getQueueFamilyPerformanceQueryPassesKHR(performanceCI);
	if (passes != 0) {
		Log::Error(
			"Vulkan-Performance", "Device requires {} passes to query the given counters, cannot create query pool.", passes);
		return false;
	}

	_queryPool = dev.createQueryPool(chain.get());
	Log::Debug("Vulkan", "Created Performance Query Pool.");

	return true;
}

void PerformanceQueryPool::BeginCommandBuffer(vk::CommandBuffer cmd) {
	if (!_queryPool) { return; }

	auto dev = _device.GetDevice();
	dev.resetQueryPool(_queryPool, 0, 1);
	cmd.beginQuery(_queryPool, 0, {});

	const vk::MemoryBarrier barrier(vk::AccessFlagBits::eMemoryWrite,
	                                vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite);
	cmd.pipelineBarrier(
		vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, barrier, nullptr, nullptr);
}

void PerformanceQueryPool::EndCommandBuffer(vk::CommandBuffer cmd) {
	if (!_queryPool) { return; }

	auto dev = _device.GetDevice();

	const vk::MemoryBarrier barrier(vk::AccessFlagBits::eMemoryWrite,
	                                vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite);
	cmd.pipelineBarrier(
		vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, barrier, nullptr, nullptr);
	cmd.endQuery(_queryPool, 0);
}

void PerformanceQueryPool::Report() {
	if (!_queryPool) {
		Log::Error("Vulkan", "Attempting to report Query Pool results before setting up a Query Pool!");
		return;
	}

	auto dev = _device.GetDevice();
	auto result =
		dev.getQueryPoolResults<vk::PerformanceCounterResultKHR>(_queryPool,
	                                                           0,
	                                                           1,
	                                                           _results.size() * sizeof(vk::PerformanceCounterResultKHR),
	                                                           sizeof(vk::PerformanceCounterResultKHR),
	                                                           vk::QueryResultFlagBits::eWait);
	if (result.result != vk::Result::eSuccess) {
		Log::Error("Vulkan", "Failed to fetch Query Pool results!");
		return;
	}
	_results = result.value;

	Log::Info("Vulkan-Performance", "===== Performance Query Report =====");
	for (size_t i = 0; i < _counters.size(); ++i) {
		const auto& counter = _counters[i];
		const auto& desc    = _counterDescriptions[i];
		const auto& result  = _results[i];

		switch (counter.storage) {
			case vk::PerformanceCounterStorageKHR::eInt32:
				Log::Info("Vulkan-Performance",
				          "\t{} ({}): {} {}",
				          desc.name,
				          desc.description,
				          result.int32,
				          UnitToString(counter.unit));
				break;
			case vk::PerformanceCounterStorageKHR::eInt64:
				Log::Info("Vulkan-Performance",
				          "\t{} ({}): {} {}",
				          desc.name,
				          desc.description,
				          result.int64,
				          UnitToString(counter.unit));
				break;
			case vk::PerformanceCounterStorageKHR::eUint32:
				Log::Info("Vulkan-Performance",
				          "\t{} ({}): {} {}",
				          desc.name,
				          desc.description,
				          result.uint32,
				          UnitToString(counter.unit));
				break;
			case vk::PerformanceCounterStorageKHR::eUint64:
				Log::Info("Vulkan-Performance",
				          "\t{} ({}): {} {}",
				          desc.name,
				          desc.description,
				          result.uint64,
				          UnitToString(counter.unit));
				break;
			case vk::PerformanceCounterStorageKHR::eFloat32:
				Log::Info("Vulkan-Performance",
				          "\t{} ({}): {} {}",
				          desc.name,
				          desc.description,
				          result.float32,
				          UnitToString(counter.unit));
				break;
			case vk::PerformanceCounterStorageKHR::eFloat64:
				Log::Info("Vulkan-Performance",
				          "\t{} ({}): {} {}",
				          desc.name,
				          desc.description,
				          result.float64,
				          UnitToString(counter.unit));
				break;
			default:
				break;
		}
	}
	Log::Info("Vulkan-Performance", "====================================");
}

void PerformanceQueryPool::LogCounters(const std::vector<vk::PerformanceCounterKHR>& counters,
                                       const std::vector<vk::PerformanceCounterDescriptionKHR>& descriptions) {
	for (size_t i = 0; i < counters.size(); ++i) {
		const auto& counter = counters[i];
		const auto& desc    = descriptions[i];

		Log::Info("Vulkan-Performance", "\t{}: {}", desc.name, desc.description);
		Log::Info("Vulkan-Performance", "\t\tStorage: {}", vk::to_string(counter.storage));
		Log::Info("Vulkan-Performance", "\t\tScope: {}", vk::to_string(counter.scope));
		Log::Info("Vulkan-Performance", "\t\tUnit: {}", vk::to_string(counter.unit));
	}
}

void QueryPoolResultDeleter::operator()(QueryPoolResult* result) {
	result->_device._queryPoolResultPool.Free(result);
}

QueryPoolResult::QueryPoolResult(Device& device, bool deviceTimebase)
		: _device(device), _deviceTimebase(deviceTimebase) {}

void QueryPoolResult::SignalTimestampTicks(uint64_t ticks) {
	_timestampTicks = ticks;
	_hasTimestamp   = true;
}

QueryPool::QueryPool(Device& device) : _device(device) {
	_supportsTimestamp = _device.GetDeviceInfo().Properties.Core.limits.timestampComputeAndGraphics;
	if (_supportsTimestamp) { AddPool(); }
}

QueryPool::~QueryPool() noexcept {
	auto dev = _device.GetDevice();

	for (auto& pool : _pools) { dev.destroyQueryPool(pool.Pool); }
}

void QueryPool::Begin() {
	auto dev = _device.GetDevice();

	for (size_t i = 0; i <= _poolIndex; ++i) {
		if (i >= _pools.size()) { continue; }

		auto& pool = _pools[i];
		if (pool.Index == 0) { continue; }

		auto results      = dev.getQueryPoolResults<uint64_t>(pool.Pool,
                                                     0,
                                                     pool.Index,
                                                     pool.Index * sizeof(uint64_t),
                                                     sizeof(uint64_t),
                                                     vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
		pool.QueryResults = results.value;
		for (uint32_t j = 0; j < pool.Index; ++j) { pool.Cookies[j]->SignalTimestampTicks(pool.QueryResults[j]); }
		if (_device.GetDeviceInfo().EnabledFeatures.Vulkan12.hostQueryReset) {
			dev.resetQueryPool(pool.Pool, 0, pool.Index);
		}
	}

	_poolIndex = 0;
	for (auto& pool : _pools) { pool.Index = 0; }
}

QueryPoolResultHandle QueryPool::WriteTimestamp(vk::CommandBuffer cmd, vk::PipelineStageFlagBits stage) {
	if (!_supportsTimestamp) { return {}; }

	if (_pools[_poolIndex].Index >= _pools[_poolIndex].Size) { _poolIndex++; }
	if (_poolIndex >= _pools.size()) { AddPool(); }

	auto& pool               = _pools[_poolIndex];
	auto cookie              = QueryPoolResultHandle(_device._queryPoolResultPool.Allocate(_device, true));
	pool.Cookies[pool.Index] = cookie;

	if (!_device.GetDeviceInfo().EnabledFeatures.Vulkan12.hostQueryReset) {
		cmd.resetQueryPool(pool.Pool, pool.Index, 1);
	}
	cmd.writeTimestamp(stage, pool.Pool, pool.Index);
	pool.Index++;

	return cookie;
}

void QueryPool::AddPool() {
	const vk::QueryPoolCreateInfo poolCI({}, vk::QueryType::eTimestamp, 64, {});

	auto dev = _device.GetDevice();
	Pool pool;
	pool.Pool  = dev.createQueryPool(poolCI);
	pool.Size  = poolCI.queryCount;
	pool.Index = 0;
	pool.QueryResults.resize(pool.Size);
	pool.Cookies.resize(pool.Size);

	if (_device.GetDeviceInfo().EnabledFeatures.Vulkan12.hostQueryReset) { dev.resetQueryPool(pool.Pool, 0, pool.Size); }
	_pools.push_back(std::move(pool));
}
}  // namespace Vulkan
}  // namespace Luna
