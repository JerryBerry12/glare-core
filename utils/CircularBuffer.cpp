/*=====================================================================
CircularBuffer.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-05-16 16:42:23 +0100
=====================================================================*/
#include "CircularBuffer.h"


#if BUILD_TESTS


#include "../indigo/TestUtils.h"
#include "../utils/timer.h"
#include "../indigo/globals.h"


void circularBufferTest()
{
	//======================== Test construction and destruction ========================
	{
		CircularBuffer<int> buf;
	}

	//======================== Test push_back ========================
	{
		CircularBuffer<int> buf;
		buf.push_back(1);

		testAssert(buf.begin == 0);
		testAssert(buf.size() == 1);
		testAssert(buf.front() == 1);
		testAssert(buf.back() == 1);

		buf.push_back(2);

		testAssert(buf.begin == 0);
		testAssert(buf.size() == 2);
		testAssert(buf.front() == 1);
		testAssert(buf.back() == 2);

		buf.push_back(3);

		testAssert(buf.begin == 0);
		testAssert(buf.size() == 3);
		testAssert(buf.front() == 1);
		testAssert(buf.back() == 3);
	}

	{
		CircularBuffer<int> buf;

		for(size_t i=0; i<1000; ++i)
		{
			buf.push_back((int)i);

			testAssert(buf.begin == 0);
			testAssert(buf.size() == i + 1);
			testAssert(buf.front() == 0);
			testAssert(buf.back() == i);
		}
	}


	//======================== Test push_front ========================
	{
		CircularBuffer<int> buf;
		buf.push_front(100);

		testAssert(buf.begin == 1);
		testAssert(buf.end == 0);
		testAssert(buf.size() == 1);
		testAssert(buf.front() == 100);
		testAssert(buf.back() == 100);

		buf.push_front(99);

		testAssert(buf.begin == 0);
		testAssert(buf.end == 0);
		testAssert(buf.size() == 2);
		testAssert(buf.front() == 99);
		testAssert(buf.back() == 100);

		buf.push_front(98);

		testAssert(buf.begin == 3);
		testAssert(buf.end == 2);
		testAssert(buf.size() == 3);
		testAssert(buf.front() == 98);
		testAssert(buf.back() == 100);

		buf.push_front(97);

		testAssert(buf.begin == 2);
		testAssert(buf.end == 2);
		testAssert(buf.size() == 4);
		testAssert(buf.front() == 97);
		testAssert(buf.back() == 100);

		buf.push_front(96);

		testAssert(buf.begin == 1);
		testAssert(buf.end == 6);
		testAssert(buf.size() == 5);
		testAssert(buf.front() == 96);
		testAssert(buf.back() == 100);

		buf.push_front(95);

		testAssert(buf.begin == 0);
		testAssert(buf.end == 6);
		testAssert(buf.size() == 6);
		testAssert(buf.front() == 95);
		testAssert(buf.back() == 100);

		buf.push_front(94);

		testAssert(buf.begin == 7);
		testAssert(buf.end == 6);
		testAssert(buf.size() == 7);
		testAssert(buf.front() == 94);
		testAssert(buf.back() == 100);

		buf.push_front(93);

		testAssert(buf.begin == 6);
		testAssert(buf.end == 6);
		testAssert(buf.size() == 8);
		testAssert(buf.front() == 93);
		testAssert(buf.back() == 100);
	}

	{
		CircularBuffer<int> buf;

		for(size_t i=0; i<1000; ++i)
		{
			buf.push_front((int)i);

			testAssert(buf.size() == i + 1);
			testAssert(buf.front() == i);
			testAssert(buf.back() == 0);
		}
	}


	//======================== Test pop_back ========================
	{
		CircularBuffer<int> buf;
		buf.push_back(1);
		buf.pop_back();
		testAssert(buf.size() == 0);
	}


	{
		CircularBuffer<int> buf;

		// Make a queue (0, 1, 2, ... n-2, n-1)
		const size_t N = 1000;
		for(size_t i=0; i<N; ++i)
		{
			buf.push_back((int)i);

			testAssert(buf.front() == 0);
			testAssert(buf.back() == (int)i);
			testAssert(buf.size() == i + 1);
		}

		for(int i=(int)N-1; i>=0; --i)
		{
			testAssert(buf.back() == i);
			buf.pop_back();
		}

		testAssert(buf.size() == 0);
	}

	//======================== Test pop_front ========================
	{
		CircularBuffer<int> buf;
		buf.push_front(1);
		buf.pop_front();
		testAssert(buf.size() == 0);
	}


	{
		CircularBuffer<int> buf;

		// Make a queue (0, 1, 2, ... n-2, n-1)
		const size_t N = 1000;
		for(size_t i=0; i<N; ++i)
		{
			buf.push_back((int)i);
			testAssert(buf.size() == i + 1);
		}

		for(size_t i=0; i<N; ++i)
		{
			testAssert(buf.front() == i);
			buf.pop_front();
		}

		testAssert(buf.size() == 0);
	}


	//======================== Test a mix of push_front and push_back ========================
	{
		CircularBuffer<int> buf;

		// Make a queue (n-1, n-2, ...., 2, 1, 0, 0, 1, 2, ... n-2, n-1)
		const size_t N = 1000;
		for(size_t i=0; i<N; ++i)
		{
			buf.push_front((int)i);
			buf.push_back((int)i);

			testAssert(buf.front() == (int)i);
			testAssert(buf.back() == (int)i);
			testAssert(buf.size() == 2*(i+1));
		}

		testAssert(buf.size() == 2*N);

		// Remove (n-1, n-2, ...., 2, 1, 0) prefix
		for(int i=(int)N-1; i>=0; --i)
		{
			testAssert(buf.front() == i);
			buf.pop_front();
		}

		testAssert(buf.size() == N);

		// Remove (0, 1, 2, ... n-2, n-1)
		for(size_t i=0; i<N; ++i)
		{
			testAssert(buf.front() == i);
			buf.pop_front();
		}

		testAssert(buf.size() == 0);
	}

}


#endif
