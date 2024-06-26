/*=====================================================================
MyThread.cpp
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MyThread.h"


#include "StringUtils.h"
#include "PlatformUtils.h"
#include "Exception.h"
#if BUGSPLAT_SUPPORT
#include <BugSplat.h>
#endif
#include <cassert>
#if defined(_WIN32)
#include <process.h>
#else
#include <errno.h>
#endif


MyThread::MyThread()
:	thread_handle(0),
	joined(false)
{
}


MyThread::~MyThread()
{
#if defined(_WIN32)
	if(thread_handle != 0)
		if(!CloseHandle(thread_handle))
		{
			assert(0);
			//std::cerr << "ERROR: CloseHandle on thread failed." << std::endl;
		}
#else
	// Mark the thread resources as freeable if needed.
	// Each pthread must either have pthread_join() or pthread_detach() call on it.
	// So we'll call pthread_detach() only if we haven't joined it.
	// See: http://www.kernel.org/doc/man-pages/online/pages/man3/pthread_detach.3.html
	if(!joined)
	{
		const int result = pthread_detach(thread_handle);
		assertOrDeclareUsed(result == 0);
	}
#endif
}


#if defined(_WIN32)
	static unsigned int __stdcall
#else
	static void*
#endif
threadFunction(void* the_thread_)
{
#if BUGSPLAT_SUPPORT
	SetPerThreadCRTExceptionBehavior(); // For BugSplat: This call is needed in each thread of the app.
#endif

	MyThread* the_thread = static_cast<MyThread*>(the_thread_);

	the_thread->run();

	// Decrement the reference count
	const int64 prev_ref_count = the_thread->decRefCount();
	assert(prev_ref_count >= 0);
	if(prev_ref_count == 1)
		delete the_thread;

	return 0;
}


void MyThread::launch()
{
	assert(thread_handle == 0);

	// Increment the thread reference count.
	// This is because there is a pointer to the the thread that is passed as an argument to _beginthreadex() ('this' passed as the arglist param),
	// that isn't a Reference object.
	this->incRefCount();

#if defined(_WIN32)
	thread_handle = (HANDLE)_beginthreadex(
		NULL, // security
		0, // stack size
		threadFunction, // startAddress
		this, // arglist
		0, // Initflag, 0 = running
		NULL // thread identifier out.
	);

	if(thread_handle == 0)
		throw glare::Exception("Thread creation failed.");
#else
	const int result = pthread_create(
		&thread_handle, // Thread
		NULL, // attr
		threadFunction, // start routine
		this // arg
	);
	if(result != 0)
	{
		if(result == EAGAIN)
			throw glare::Exception("Thread creation failed.  (EAGAIN)");
		else if(result == EINVAL)
			throw glare::Exception("Thread creation failed.  (EINVAL)");
		else if(result == EPERM)
			throw glare::Exception("Thread creation failed.  (EPERM)");
		else
			throw glare::Exception("Thread creation failed.  (Error code: " + toString(result) + ")");
	}
#endif
}


void MyThread::join() // Wait for thread termination
{
	joined = true;
#if defined(_WIN32)
	/*const DWORD result =*/ ::WaitForSingleObject(thread_handle, INFINITE);
#else
	const int result = pthread_join(thread_handle, NULL);
	assertOrDeclareUsed(result == 0);
#endif
}


void MyThread::setPriority(Priority p)
{
#if defined(_WIN32)
	int pri = THREAD_PRIORITY_NORMAL;
	if(p == Priority_Normal)
		pri = THREAD_PRIORITY_NORMAL;
	else if(p == Priority_BelowNormal)
		pri = THREAD_PRIORITY_BELOW_NORMAL;
	else if(p == Priority_Lowest)
		pri = THREAD_PRIORITY_LOWEST;
	else if(p == Priority_Idle)
		pri = THREAD_PRIORITY_IDLE;
	else
	{
		assert(0);
	}
	const BOOL res = ::SetThreadPriority(thread_handle, pri);
	if(res == 0)
		throw glare::Exception("SetThreadPriority failed: " + PlatformUtils::getLastErrorString());
#else
	throw glare::Exception("Can't change thread priority after creation on Linux or OS X");
	/*// Get current priority
	int policy;
	sched_param param;
	const int ret = pthread_getschedparam(thread_handle, &policy, &param);
	if(ret != 0)
		throw glare::Exception("pthread_getschedparam failed: " + toString(ret));
	std::cout << "param.sched_priority: " << param.sched_priority << std::endl;

	std::cout << "SCHED_OTHER: " << SCHED_OTHER << std::endl;
	std::cout << "policy: " << policy << std::endl;


	//sched_param param;
	param.sched_priority = -1;
	const int res = pthread_setschedparam(thread_handle, policy, &param);
	if(res != 0)
		throw glare::Exception("pthread_setschedparam failed: " + toString(res));*/
#endif
}


#if defined(_WIN32)
void MyThread::setAffinity(int32 group, uint64 proc_affinity_mask)
{
	GROUP_AFFINITY affinity;
	ZeroMemory(&affinity, sizeof(GROUP_AFFINITY));
	affinity.Group = (WORD)group;
	affinity.Mask = proc_affinity_mask;

	if(SetThreadGroupAffinity(thread_handle, &affinity, NULL) == 0)
		throw glare::Exception("SetThreadGroupAffinity failed: " + PlatformUtils::getLastErrorString());
}
#endif
