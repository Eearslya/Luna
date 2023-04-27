#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Tracy/Tracy.hpp>
#include <sstream>

namespace Luna {
static struct ThreadingState {
	ThreadSafeObjectPool<Task> TaskPool;
	ThreadSafeObjectPool<TaskGroup> TaskGroupPool;
	ThreadSafeObjectPool<TaskDependencies> TaskDependenciesPool;

	std::queue<Task*> Tasks;
	std::condition_variable TasksCondition;
	std::mutex TasksMutex;
	std::atomic_uint TasksCompleted;
	std::atomic_uint TasksTotal;
	std::condition_variable WaitCondition;
	std::mutex WaitMutex;

	std::atomic_bool Running = false;
	std::vector<std::thread> WorkerThreads;
	std::vector<std::string> SysThreadIDs;
} State;

static thread_local uint32_t ThreadID = ~0u;
static thread_local std::thread::id SysThreadID;

void Threading::SetThreadID(uint32_t thread) {
	ThreadID = thread;
}

uint32_t Threading::GetThreadID() {
	return ThreadID;
}

uint32_t Threading::GetThreadIDFromSys(const std::string& idStr) {
	for (int i = 0; i < State.SysThreadIDs.size(); ++i) {
		if (State.SysThreadIDs[i] == idStr) { return i; }
	}

	return std::numeric_limits<uint32_t>::max();
}

void TaskDependenciesDeleter::operator()(TaskDependencies* deps) {
	Threading::FreeTaskDependencies(deps);
}

TaskDependencies::TaskDependencies() {
	PendingCount.store(0, std::memory_order_relaxed);
	DependencyCount.store(1, std::memory_order_relaxed);
}

void TaskDependencies::DependencySatisfied() {
	const auto dependencyCountBefore = DependencyCount.fetch_sub(1, std::memory_order_acq_rel);
	if (dependencyCountBefore == 1) {
		if (PendingTasks.empty()) {
			NotifyDependees();
		} else {
			Threading::SubmitTasks(PendingTasks);
			PendingTasks.clear();
		}
	}
}

void TaskDependencies::NotifyDependees() {
	for (auto& dependee : Pending) { dependee->DependencySatisfied(); }
	Pending.clear();

	std::unique_lock<std::mutex> lock(Mutex);
	Done = true;
	Condition.notify_all();
}

void TaskDependencies::TaskCompleted() {
	const auto taskCountBefore = PendingCount.fetch_sub(1, std::memory_order_acq_rel);
	if (taskCountBefore == 1) { NotifyDependees(); }
}

Task::Task(TaskDependenciesHandle dependencies, std::function<void()>&& function)
		: Dependencies(dependencies), Function(std::move(function)) {}

void TaskGroupDeleter::operator()(TaskGroup* group) {
	Threading::FreeTaskGroup(group);
}

TaskGroup::~TaskGroup() noexcept {
	if (!Flushed) { Flush(); }
}

void TaskGroup::AddFlushDependency() {
	Dependencies->DependencyCount.fetch_add(1, std::memory_order_relaxed);
}

void TaskGroup::DependOn(TaskGroup& dependency) {
	Threading::AddDependency(*this, dependency);
}

void TaskGroup::Enqueue(std::function<void()>&& function) {
	if (Flushed) { throw std::logic_error("Cannot add tasks to TaskGroup after being flushed!"); }

	Dependencies->PendingTasks.push_back(State.TaskPool.Allocate(Dependencies, std::move(function)));
	Dependencies->PendingCount.fetch_add(1, std::memory_order_relaxed);
}

void TaskGroup::Flush() {
	if (Flushed) { throw std::logic_error("TaskGroup cannot be flushed twice!"); }

	Flushed = true;
	ReleaseFlushDependency();
}

void TaskGroup::ReleaseFlushDependency() {
	Dependencies->DependencySatisfied();
}

void TaskGroup::Wait() {
	if (!Flushed) { Flush(); }

	std::unique_lock<std::mutex> lock(Dependencies->Mutex);
	Dependencies->Condition.wait(lock, [this]() { return Dependencies->Done; });
}

void TaskComposer::AddOutgoingDependency(TaskGroup& task) {
	Threading::AddDependency(task, *GetOutgoingTask());
}

TaskGroup& TaskComposer::BeginPipelineStage() {
	auto newGroup        = Threading::CreateTaskGroup();
	auto newDependencies = Threading::CreateTaskGroup();
	if (_current) { Threading::AddDependency(*newDependencies, *_current); }
	if (_nextStageDependencies) { Threading::AddDependency(*newDependencies, *_nextStageDependencies); }
	_nextStageDependencies.Reset();
	Threading::AddDependency(*newGroup, *newDependencies);

	_current              = std::move(newGroup);
	_incomingDependencies = std::move(newDependencies);

	return *_current;
}

TaskGroupHandle TaskComposer::GetDeferredEnqueueHandle() {
	if (!_nextStageDependencies) { _nextStageDependencies = Threading::CreateTaskGroup(); }

	return _nextStageDependencies;
}

TaskGroup& TaskComposer::GetGroup() {
	return bool(_current) ? *_current : BeginPipelineStage();
}

TaskGroupHandle TaskComposer::GetOutgoingTask() {
	BeginPipelineStage();
	auto ret = std::move(_incomingDependencies);
	_incomingDependencies.Reset();
	_current.Reset();

	return ret;
}

TaskGroupHandle TaskComposer::GetPipelineStageDependency() {
	return _incomingDependencies;
}

void TaskComposer::SetIncomingTask(TaskGroupHandle group) {
	_current = std::move(group);
}

bool Threading::Initialize() {
	ZoneScopedN("Threading::Initialize");

	SetThreadID(0);
	SysThreadID = std::this_thread::get_id();
	std::ostringstream oss;
	oss << SysThreadID;
	State.SysThreadIDs.push_back(oss.str());

	const auto threadCount = std::thread::hardware_concurrency();
	Log::Debug("Threading", "Starting {} worker threads.", threadCount);
	State.Running = true;
	State.SysThreadIDs.resize(threadCount + 1);
	for (int i = 0; i < threadCount; ++i) {
		State.WorkerThreads.emplace_back([i]() { WorkerThread(i + 1); });
	}

	return true;
}

void Threading::Shutdown() {
	ZoneScopedN("Threading::Shutdown");

	{
		std::unique_lock<std::mutex> lock(State.TasksMutex);
		State.Running = false;
	}
	State.TasksCondition.notify_all();
	for (auto& thread : State.WorkerThreads) { thread.join(); }
}

uint32_t Threading::GetThreadCount() {
	return static_cast<uint32_t>(State.WorkerThreads.size());
}

void Threading::AddDependency(TaskGroup& dependee, TaskGroup& dependency) {
	if (dependee.Flushed || dependency.Flushed) {
		throw std::logic_error("Cannot add a dependency if either group has already been flushed.");
	}

	dependency.Dependencies->Pending.push_back(dependee.Dependencies);
	dependee.Dependencies->DependencyCount.fetch_add(1, std::memory_order_relaxed);
}

TaskGroupHandle Threading::CreateTaskGroup() {
	TaskGroupHandle group(State.TaskGroupPool.Allocate());
	group->Dependencies = TaskDependenciesHandle(State.TaskDependenciesPool.Allocate());
	group->Dependencies->PendingCount.store(0, std::memory_order_relaxed);

	return group;
}

void Threading::Submit(TaskGroupHandle& group) {
	group->Flush();
	group.Reset();
}

void Threading::SubmitTasks(const std::vector<Task*>& tasks) {
	std::lock_guard<std::mutex> lock(State.TasksMutex);
	State.TasksTotal.fetch_add(tasks.size(), std::memory_order_relaxed);

	for (auto& task : tasks) { State.Tasks.push(task); }

	const size_t count = tasks.size();
	if (count >= State.WorkerThreads.size()) {
		State.TasksCondition.notify_all();
	} else {
		for (size_t i = 0; i < count; ++i) { State.TasksCondition.notify_one(); }
	}
}

void Threading::WaitIdle() {
	std::unique_lock<std::mutex> lock(State.WaitMutex);
	State.WaitCondition.wait(lock, []() {
		return State.TasksTotal.load(std::memory_order_relaxed) == State.TasksCompleted.load(std::memory_order_relaxed);
	});
}

void Threading::FreeTaskDependencies(TaskDependencies* deps) {
	State.TaskDependenciesPool.Free(deps);
}

void Threading::FreeTaskGroup(TaskGroup* group) {
	State.TaskGroupPool.Free(group);
}

void Threading::WorkerThread(int threadID) {
	Log::Trace("Threading", "Starting worker thread {}.", threadID);

	SetThreadID(threadID);
	SysThreadID = std::this_thread::get_id();
	std::ostringstream oss;
	oss << SysThreadID;
	State.SysThreadIDs[threadID] = oss.str();

	while (State.Running) {
		Task* task = nullptr;
		{
			std::unique_lock<std::mutex> lock(State.TasksMutex);
			State.TasksCondition.wait(lock, []() { return !State.Running || !State.Tasks.empty(); });

			if (!State.Running && State.Tasks.empty()) { break; }

			task = State.Tasks.front();
			State.Tasks.pop();
		}

		if (task->Function) {
			try {
				task->Function();
			} catch (const std::exception& e) {
				Log::Error("Threading", "Exception encountered when running task: {}", e.what());
			}
		}

		task->Dependencies->TaskCompleted();
		State.TaskPool.Free(task);

		{
			const auto completedCount = State.TasksCompleted.fetch_add(1, std::memory_order_relaxed) + 1;
			if (completedCount == State.TasksTotal.load(std::memory_order_relaxed)) {
				std::lock_guard<std::mutex> lock(State.WaitMutex);
				State.WaitCondition.notify_all();
			}
		}
	}

	Log::Trace("Threading", "Stopping worker thread {}.", threadID);
}
}  // namespace Luna
