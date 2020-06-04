/*=====================================================================
TaskRunnerThread.cpp
--------------------
Copyright Glare Technologies Limited 2020 -
Generated at 2011-10-05 22:17:09 +0100
=====================================================================*/
#include "TaskRunnerThread.h"


#include "TaskManager.h"
#include "Task.h"
#include "ConPrint.h"
#include "StringUtils.h"
#include "Timer.h"
#include "Clock.h"
#include "PlatformUtils.h"


//#define TASK_STATS 1
#if TASK_STATS
#define TASK_STATS_DO(expr) (expr)
#else
#define TASK_STATS_DO(expr)
#endif


namespace Indigo
{


TaskRunnerThread::TaskRunnerThread(TaskManager* manager_, size_t thread_index_)
:	manager(manager_),
	thread_index(thread_index_)
//	task_times(NULL)
{
#if TASK_STATS
	task_times = new std::vector<TaskTimes>();
#endif
}


TaskRunnerThread::~TaskRunnerThread()
{
#if TASK_STATS
	for(size_t i=0; i<task_times->size(); ++i)
	{
		for(size_t z=0; z<i; ++z)
			conPrintStr("\t");

		conPrint("thread " + toString(thread_index) + ": task " + toString(i) + " start=" + doubleToStringNSigFigs((*task_times)[i].dequeue_time, 12) + " s, exec took " +
			::doubleToStringNSigFigs((*task_times)[i].finish_time - (*task_times)[i].dequeue_time, 5) + " s");
	}

	delete task_times;
#endif
}


void TaskRunnerThread::run()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled(manager->getName() + " thread " + toString(thread_index));

	while(1)
	{
		Reference<Task> task = manager->dequeueTask();
		this->current_task = task;
		if(task.isNull())
			break; // All tasks are finished, terminate thread by returning from run().

#if TASK_STATS
		task_times->push_back(TaskTimes());
		task_times->back().dequeue_time = Clock::getCurTimeRealSec();
#endif
		
		// Execute the task
		task->run(thread_index);

		TASK_STATS_DO(task_times->back().finish_time = Clock::getCurTimeRealSec());

		this->current_task = NULL;

		// Inform the task manager that we have executed the task.
		manager->taskFinished();
	}
}


void TaskRunnerThread::cancelTasks()
{
	Reference<Task> cur_task = this->current_task;
	if(cur_task.nonNull())
		cur_task->cancelTask();
}


} // end namespace Indigo 
