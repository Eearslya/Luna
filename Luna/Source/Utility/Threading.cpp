#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>

namespace Luna {
Threading* Threading::_instance       = nullptr;
static thread_local uint32_t ThreadID = ~0u;

void Threading::SetThreadID(uint32_t thread) {
	ThreadID = thread;
}

uint32_t Threading::GetThreadID() {
	return ThreadID;
}

void TaskDependenciesDeleter::operator()(TaskDependencies* deps) {
	Threading::Get()->FreeTaskDependencies(deps);
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
			Threading::Get()->SubmitTasks(PendingTasks);
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
	Threading::Get()->FreeTaskGroup(group);
}

TaskGroup::~TaskGroup() noexcept {
	if (!Flushed) { Flush(); }
}

void TaskGroup::AddFlushDependency() {
	Dependencies->DependencyCount.fetch_add(1, std::memory_order_relaxed);
}

void TaskGroup::DependOn(TaskGroup& dependency) {
	Threading::Get()->AddDependency(*this, dependency);
}

void TaskGroup::Enqueue(std::function<void()>&& function) {
	if (Flushed) { throw std::logic_error("Cannot add tasks to TaskGroup after being flushed!"); }

	Dependencies->PendingTasks.push_back(Threading::Get()->_taskPool.Allocate(Dependencies, std::move(function)));
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
	Threading::Get()->AddDependency(task, *GetOutgoingTask());
}

TaskGroup& TaskComposer::BeginPipelineStage() {
	auto* threading = Threading::Get();

	auto newGroup        = threading->CreateTaskGroup();
	auto newDependencies = threading->CreateTaskGroup();
	if (_current) { threading->AddDependency(*newDependencies, *_current); }
	if (_nextStageDependencies) { threading->AddDependency(*newDependencies, *_nextStageDependencies); }
	_nextStageDependencies.Reset();
	threading->AddDependency(*newGroup, *newDependencies);

	_current              = std::move(newGroup);
	_incomingDependencies = std::move(newDependencies);

	return *_current;
}

TaskGroupHandle TaskComposer::GetDeferredEnqueueHandle() {
	if (!_nextStageDependencies) { _nextStageDependencies = Threading::Get()->CreateTaskGroup(); }

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

Threading::Threading() {
	if (_instance) { throw std::runtime_error("Cannot initialize Threading more than once!"); }
	_instance = this;

	SetThreadID(0);

	const auto threadCount = std::thread::hardware_concurrency();
	Log::Debug("Threading", "Starting {} worker threads.", threadCount);
	_running = true;
	for (int i = 0; i < threadCount; ++i) {
		_workerThreads.emplace_back([this, i]() { WorkerThread(i + 1); });
	}
}

Threading::~Threading() noexcept {
	{
		std::unique_lock<std::mutex> lock(_tasksMutex);
		_running = false;
	}
	_tasksCondition.notify_all();
	for (auto& thread : _workerThreads) { thread.join(); }

	_instance = nullptr;
}

uint32_t Threading::GetThreadCount() const {
	return static_cast<uint32_t>(_workerThreads.size());
}

void Threading::AddDependency(TaskGroup& dependee, TaskGroup& dependency) {
	if (dependee.Flushed || dependency.Flushed) {
		throw std::logic_error("Cannot add a dependency if either group has already been flushed.");
	}

	dependency.Dependencies->Pending.push_back(dependee.Dependencies);
	dependee.Dependencies->DependencyCount.fetch_add(1, std::memory_order_relaxed);
}

TaskGroupHandle Threading::CreateTaskGroup() {
	TaskGroupHandle group(_taskGroupPool.Allocate());
	group->Dependencies = TaskDependenciesHandle(_taskDependenciesPool.Allocate());
	group->Dependencies->PendingCount.store(0, std::memory_order_relaxed);

	return group;
}

void Threading::Submit(TaskGroupHandle& group) {
	group->Flush();
	group.Reset();
}

void Threading::SubmitTasks(const std::vector<Task*>& tasks) {
	std::lock_guard<std::mutex> lock(_tasksMutex);
	_tasksTotal.fetch_add(tasks.size(), std::memory_order_relaxed);

	for (auto& task : tasks) { _tasks.push(task); }

	const size_t count = tasks.size();
	if (count >= _workerThreads.size()) {
		_tasksCondition.notify_all();
	} else {
		for (size_t i = 0; i < count; ++i) { _tasksCondition.notify_one(); }
	}
}

void Threading::WaitIdle() {
	std::unique_lock<std::mutex> lock(_waitMutex);
	_waitCondition.wait(lock, [this]() {
		return _tasksTotal.load(std::memory_order_relaxed) == _tasksCompleted.load(std::memory_order_relaxed);
	});
}

void Threading::FreeTaskDependencies(TaskDependencies* deps) {
	_taskDependenciesPool.Free(deps);
}

void Threading::FreeTaskGroup(TaskGroup* group) {
	_taskGroupPool.Free(group);
}

void Threading::WorkerThread(int threadID) {
	Log::Trace("Threading", "Starting worker thread {}.", threadID);

	SetThreadID(threadID);

	while (_running) {
		Task* task = nullptr;
		{
			std::unique_lock<std::mutex> lock(_tasksMutex);
			_tasksCondition.wait(lock, [this]() { return !_running || !_tasks.empty(); });

			if (!_running && _tasks.empty()) { break; }

			task = _tasks.front();
			_tasks.pop();
		}

		if (task->Function) {
			try {
				task->Function();
			} catch (const std::exception& e) {
				Log::Error("Threading", "Exception encountered when running task: {}", e.what());
			}
		}

		task->Dependencies->TaskCompleted();
		_taskPool.Free(task);

		{
			const auto completedCount = _tasksCompleted.fetch_add(1, std::memory_order_relaxed) + 1;
			if (completedCount == _tasksTotal.load(std::memory_order_relaxed)) {
				std::lock_guard<std::mutex> lock(_waitMutex);
				_waitCondition.notify_all();
			}
		}
	}

	Log::Trace("Threading", "Stopping worker thread {}.", threadID);
}
}  // namespace Luna
