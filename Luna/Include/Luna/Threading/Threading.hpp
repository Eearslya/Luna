#pragma once

#include <Luna/Core/Module.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <atomic>
#include <condition_variable>
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

class Threading : public Module::Registrar<Threading> {
	static const inline bool Registered = Register("Threading", Stage::Never);

	friend struct TaskDependenciesDeleter;
	friend struct TaskGroup;
	friend struct TaskGroupDeleter;

 public:
	Threading();
	~Threading() noexcept;

	virtual void Update() override {}

	void AddDependency(TaskGroup& dependee, TaskGroup& dependency);
	TaskGroupHandle CreateTaskGroup();
	void Submit(TaskGroupHandle& group);
	void SubmitTasks(const std::vector<Task*>& tasks);
	void WaitIdle();

 private:
	void FreeTaskDependencies(TaskDependencies* dependencies);
	void FreeTaskGroup(TaskGroup* group);
	void WorkerThread(int threadID);

	ThreadSafeObjectPool<Task> _taskPool;
	ThreadSafeObjectPool<TaskGroup> _taskGroupPool;
	ThreadSafeObjectPool<TaskDependencies> _taskDependenciesPool;

	std::queue<Task*> _tasks;
	std::condition_variable _tasksCondition;
	std::mutex _tasksMutex;
	std::atomic_uint _tasksCompleted;
	std::atomic_uint _tasksTotal;
	std::condition_variable _waitCondition;
	std::mutex _waitMutex;

	std::atomic_bool _running = false;
	std::vector<std::thread> _workerThreads;
};
}  // namespace Luna
