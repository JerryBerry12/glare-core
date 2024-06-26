/*=====================================================================
MessageableThread.h
-------------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#pragma once


#include "MyThread.h"
#include "ThreadSafeQueue.h"
#include "ThreadMessage.h"
class ThreadManager;


/*=====================================================================
MessageableThread
-----------------
A thread that has a message queue.
Should be used in conjunction with ThreadManager.
=====================================================================*/
class MessageableThread : public MyThread
{
public:
	MessageableThread();

	virtual ~MessageableThread();

	// Deriving classes should implement this method.
	virtual void doRun() = 0;

	virtual void kill() {}

	// Called by ThreadManager.
	void setThreadManager(ThreadManager* thread_manager);

	ThreadSafeQueue<Reference<ThreadMessage> >& getMessageQueue() { return mesthread_message_queue; }
protected:
	/*
		Suspend thread for wait_period_s, while waiting on the message queue.
		Will Consume all messages on the thread message queue, and break the wait if a kill message is received,
		or if the wait period is finished.
		Has a small minimum wait time (e.g. 0.1 s)
		If the thread receives a kill message, keep_running_in_out will be set to false.
	*/
	void waitForPeriod(double wait_period_s, bool& keep_running_in_out);

	ThreadManager& getThreadManager() { return *mesthread_thread_manager; }

private:
	ThreadSafeQueue<Reference<ThreadMessage> > mesthread_message_queue; // Slightly weird and long name to avoid inheritance name clashes with other member vars.
	ThreadManager* mesthread_thread_manager;

	virtual void run();
};
