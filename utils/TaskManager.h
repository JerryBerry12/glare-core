/*=====================================================================
TaskManager.h
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "Task.h"
#include "ThreadSafeQueue.h"
#include "Mutex.h"
#include "Reference.h"
#include "Condition.h"
#include "ArrayRef.h"
#include "MyThread.h"
#include "../maths/mathstypes.h"
#include <vector>
#include <limits>
#include <string>


namespace Indigo
{


class TaskRunnerThread;


/*=====================================================================
TaskManager
-----------
Manages and runs Tasks on multiple threads.

Tests in TaskTests.
=====================================================================*/
class TaskManager
{
public:
	TaskManager(size_t num_threads = std::numeric_limits<size_t>::max());
	TaskManager(const std::string& name, size_t num_threads = std::numeric_limits<size_t>::max()); // Name is used to name TaskRunnerThreads in the debugger.

	~TaskManager();

	// Only works on Windows.  Does nothing if called on a non-windows system.
	void setThreadPriorities(MyThread::Priority priority);

	// Add, and start executing a single task.
	void addTask(const TaskRef& t);

	// Add, and start executing multiple tasks.
	void addTasks(ArrayRef<TaskRef> tasks);
	
	// Add tasks, then wait for tasks to complete.
	void runTasks(ArrayRef<TaskRef> tasks);

	size_t getNumUnfinishedTasks() const;

	bool areAllTasksComplete() const;

	/*
	Removes all tasks from the task queue.  Will not remove any tasks that have already been grabbed by a TaskRunnerThread.
	Warning: be careful with this method, as some code may be waiting with waitForTasksToComplete(), and may expect the removed
	tasks to have been completed.
	*/
	void removeQueuedTasks();

	// Blocks until all tasks have finished being executed.
	void waitForTasksToComplete();

	// Removes queued tasks, calls cancelTask() on all tasks being currently executed in TaskRunnerThreads, then blocks until all tasks have completed.
	void cancelAndWaitForTasksToComplete();

	size_t getNumThreads() const { return threads.size(); }

	bool areAllThreadsBusy();


	template <class Task, class TaskClosure> 
	void runParallelForTasks(const TaskClosure& closure, size_t begin, size_t end);

	/*
	Creates and runs some tasks in parallel.
	Stores references to the tasks in the tasks vector, and reuses them if the references are non-null.

	Task type must implement 
	set(const TaskClosure* closure, size_t begin, size_t end)
	*/
	template <class Task, class TaskClosure>
	void runParallelForTasks(const TaskClosure* closure, size_t begin, size_t end, std::vector<TaskRef>& tasks);

	/*
	In this method, each task has a particular offset and stride, where stride = num tasks.
	So each task's work indices are interleaved with each other.
	*/
	template <class Task, class TaskClosure> 
	void runParallelForTasksInterleaved(const TaskClosure& closure, size_t begin, size_t end);


	ThreadSafeQueue<TaskRef>& getTaskQueue() { return tasks; }
	const ThreadSafeQueue<TaskRef>& getTaskQueue() const { return tasks; }

	TaskRef dequeueTask(); // called by TaskRunnerThread
	void taskFinished(); // called by TaskRunnerThread
	const std::string& getName(); // called by TaskRunnerThread
private:
	void init(size_t num_threads);

	Condition num_unfinished_tasks_cond;
	mutable ::Mutex num_unfinished_tasks_mutex;
	int num_unfinished_tasks;
	
	std::string name;

	ThreadSafeQueue<TaskRef> tasks;

	std::vector<Reference<TaskRunnerThread> > threads;
};


template <class TaskType, class TaskClosure> 
void TaskManager::runParallelForTasks(const TaskClosure& closure, size_t begin, size_t end)
{
	if(begin >= end)
		return;

	const size_t num_indices = end - begin;
	const size_t num_tasks = myMax<size_t>(1, myMin(num_indices, getNumThreads()));
	const size_t num_indices_per_task = Maths::roundedUpDivide(num_indices, num_tasks);

	for(size_t t=0; t<num_tasks; ++t)
	{
		const size_t task_begin = myMin(begin + t       * num_indices_per_task, end);
		const size_t task_end   = myMin(begin + (t + 1) * num_indices_per_task, end);
		assert(task_begin >= begin && task_end <= end);

		if(task_begin < task_end)
		{
			TaskType* task = new TaskType(closure, task_begin, task_end);
			addTask(task);
		}
	}

	waitForTasksToComplete(); // The tasks should be running.  Wait for them to complete.  This blocks.
}


template <class TaskType, class TaskClosure>
void TaskManager::runParallelForTasks(const TaskClosure* closure, size_t begin, size_t end, std::vector<TaskRef>& tasks)
{
	if(begin >= end)
		return;

	const size_t num_indices = end - begin;
	const size_t num_tasks = myMax<size_t>(1, myMin(num_indices, getNumThreads()));
	const size_t num_indices_per_task = Maths::roundedUpDivide(num_indices, num_tasks);

	tasks.resize(num_tasks);

	for(size_t t=0; t<num_tasks; ++t)
	{
		if(tasks[t].isNull())
			tasks[t] = new TaskType();
		const size_t task_begin = myMin(begin + t       * num_indices_per_task, end);
		const size_t task_end   = myMin(begin + (t + 1) * num_indices_per_task, end);
		assert(task_begin >= begin && task_end <= end);
		tasks[t].downcastToPtr<TaskType>()->set(closure, task_begin, task_end);
	}

	runTasks(tasks); // Blocks
}


template <class TaskType, class TaskClosure> 
void TaskManager::runParallelForTasksInterleaved(const TaskClosure& closure, size_t begin, size_t end)
{
	if(begin >= end)
		return;

	const size_t num_indices = end - begin;
	const size_t num_tasks = myMax<size_t>(1, myMin(num_indices, getNumThreads()));
	
	for(size_t t=0; t<num_tasks; ++t)
	{
		const size_t task_begin = begin + t;
		const size_t stride = num_tasks;

		assert(task_begin >= begin);
		assert(task_begin < end);

		TaskType* task = new TaskType(closure, task_begin, end, stride);

		addTask(task);
	}

	// The tasks should be running.  Wait for them to complete.  This blocks.
	waitForTasksToComplete();
}


} // end namespace Indigo 
