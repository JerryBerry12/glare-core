//
// Generated by makeclass.rb on Sat Feb 13 21:34:45 +1300 2010.
// Copyright Nicholas Chapman.
//
#pragma once


#include "mythread.h"
#include "platformutils.h"
#include "../maths/mathstypes.h"
#include <vector>
//#include <iostream> //TEMP


template <class T>
class ParallelForThread : public MyThread
{
public:
	ParallelForThread(const T& t_, int begin_, int end_, int step_, int thread_index_) : t(t_), begin(begin_), end(end_), step(step_), thread_index(thread_index_) {}
	const T& t;
	int begin, end, step, thread_index;
	virtual void run()
	{
		for(int i=begin; i<end; ++i/*i+=step*/)
			t(i, thread_index);
	}
};


class ParallelFor
{
public:
	ParallelFor();
	~ParallelFor();
	
	template <class Task>
	static void exec(const Task& task, int begin, int end)
	{
		if(begin >= end)
			return;

		const int total = end - begin;
		const int max_num_threads = myMax(1u, PlatformUtils::getNumLogicalProcessors());
		int num_indices_per_thread = 0;
		int num_threads = 0;
		if(total <= max_num_threads)
		{
			num_threads = total;
			num_indices_per_thread = 1;
		}
		else if(total % max_num_threads == 0)
		{
			// If max_num_threads divides total perfectly
			num_threads = max_num_threads;
			num_indices_per_thread = total / num_threads;
		}
		else
		{
			num_threads = max_num_threads;
			num_indices_per_thread = (total / num_threads) + 1;
		}

		std::vector<Reference<ParallelForThread<Task> > > threads(num_threads);
		int i=begin;
		for(int t=0; t<num_threads; ++t)
		{
			const int thread_begin = i;
			const int thread_end = myMin(i + num_indices_per_thread, end);
			const int step = 1;
			//const int thread_begin = begin + thread_i;
			//const int thread_end = end;//myMin(i + num_indices_per_thread, end);
			//const int step = num_threads;

			assert(thread_begin >= begin);
			//assert(thread_end >= thread_begin);
			assert(thread_end <= end);

			threads[t] = new ParallelForThread<Task>(
				task, 
				thread_begin, // begin
				thread_end, // end
				step,
				t // thread index
			);
			threads[t]->launch(
				//false // autodelete
			);

			i += num_indices_per_thread;
		}
		assert(i >= end);

		//for(int thread_i=0; thread_i<num_threads; ++thread_i)
		//	threads[thread_i]->launch(false);

		/*for(int i=0; i<num_threads; ++i)
		{
			std::cout << "handle " << i << ": " << threads[i]->getHandle() << std::endl;
		}*/



		////////// Wait for threads to terminate //////////

#if defined(_WIN32) || defined(_WIN64)
		// If we're on Windows, use WaitForMultipleObjects() instead of waiting for each thread individually, 
		// because it seems to be somewhat faster.

		// Build array of thread handles
		std::vector<HANDLE> thread_handles(num_threads);
		for(int i=0; i<num_threads; ++i)
			thread_handles[i] = threads[i]->getHandle();

		const DWORD result = ::WaitForMultipleObjects(
			num_threads, // num handles
			&thread_handles[0], // pointer to handles
			TRUE, // bWaitAll
			INFINITE // dwMilliseconds
		);
		assert(result != WAIT_FAILED);
		if(result == WAIT_FAILED)
		{
			assert(0);
			//std::cout << "WAIT_FAILED, GetLastError ():" << GetLastError () << std::endl;
		}
		else if(result == WAIT_TIMEOUT)
		{
			assert(0);
			//std::cout << "WAIT_TIMEOUT" << std::endl;
		}

#else
		for(int i=0; i<num_threads; ++i)
			threads[i]->join();
#endif
	}
};
