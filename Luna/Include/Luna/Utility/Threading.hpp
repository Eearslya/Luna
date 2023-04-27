#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Luna {
struct Task;
struct TaskDependencies;
struct TaskGroup;

struct TaskDependenciesDeleter {
	void operator()(TaskDependencies* deps);
};

struct TaskDependencies : IntrusivePtrEnabled<TaskDependencies, TaskDependenciesDeleter, MultiThreadCounter> {
	TaskDependencies();

	void DependencySatisfied();
	void NotifyDependees();
	void TaskCompleted();

	std::vector<IntrusivePtr<TaskDependencies>> Pending;
	std::atomic_uint PendingCount;

	std::atomic_uint DependencyCount;
	std::vector<Task*> PendingTasks;

	std::condition_variable Condition;
	std::mutex Mutex;
	bool Done = false;
};
using TaskDependenciesHandle = IntrusivePtr<TaskDependencies>;

struct Task {
	Task() = default;
	Task(TaskDependenciesHandle dependencies, std::function<void()>&& function);

	TaskDependenciesHandle Dependencies;
	std::function<void()> Function;
};

struct TaskGroupDeleter {
	void operator()(TaskGroup* group);
};

struct TaskGroup : IntrusivePtrEnabled<TaskGroup, TaskGroupDeleter, MultiThreadCounter> {
	TaskGroup() = default;
	~TaskGroup() noexcept;

	void AddFlushDependency();
	void DependOn(TaskGroup& dependency);
	void Enqueue(std::function<void()>&& function);
	void Flush();
	void ReleaseFlushDependency();
	void Wait();

	TaskDependenciesHandle Dependencies;
	bool Flushed = false;
};
using TaskGroupHandle = IntrusivePtr<TaskGroup>;

class TaskComposer {
 public:
	void AddOutgoingDependency(TaskGroup& task);
	TaskGroup& BeginPipelineStage();
	TaskGroupHandle GetDeferredEnqueueHandle();
	TaskGroup& GetGroup();
	TaskGroupHandle GetOutgoingTask();
	TaskGroupHandle GetPipelineStageDependency();
	void SetIncomingTask(TaskGroupHandle group);

 private:
	TaskGroupHandle _current;
	TaskGroupHandle _incomingDependencies;
	TaskGroupHandle _nextStageDependencies;
};

class Threading final {
	friend struct TaskDependenciesDeleter;
	friend struct TaskGroup;
	friend struct TaskGroupDeleter;

 public:
	static bool Initialize();
	static void Shutdown();

	static void AddDependency(TaskGroup& dependee, TaskGroup& dependency);
	static TaskGroupHandle CreateTaskGroup();
	static uint32_t GetThreadCount();
	static void Submit(TaskGroupHandle& group);
	static void SubmitTasks(const std::vector<Task*>& tasks);
	static void WaitIdle();

	static void SetThreadID(uint32_t thread);
	static uint32_t GetThreadID();
	static uint32_t GetThreadIDFromSys(const std::string& idStr);

 private:
	static void FreeTaskDependencies(TaskDependencies* dependencies);
	static void FreeTaskGroup(TaskGroup* group);
	static void WorkerThread(int threadID);
};
}  // namespace Luna
