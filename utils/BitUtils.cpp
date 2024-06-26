/*=====================================================================
BitUtils.cpp
------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#include "BitUtils.h"


#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../indigo/globals.h"
#include "../utils/ConPrint.h"
#include "../utils/CycleTimer.h"
#include "../utils/Timer.h"
#include "../utils/StringUtils.h"


namespace BitUtils
{


void test()
{
	conPrint("BitUtils::test()");

	//===================================== bitCast =====================================
	testAssert(bitCast<uint32>(1.0f) == 0x3f800000);
	testAssert(bitCast<uint32>(5) == 5u);
	testAssert(bitCast<uint32>(-1) == 0xFFFFFFFFu); // This assumes the signed integer representation is two's complement of course.

	//===================================== lowestSetBitIndex(uint32) =====================================
	testAssert(lowestSetBitIndex(0u) == 0); // 0
	testAssert(lowestSetBitIndex(1u) == 0); // 1
	testAssert(lowestSetBitIndex(2u) == 1); // 10
	testAssert(lowestSetBitIndex(3u) == 0); // 11
	testAssert(lowestSetBitIndex(4u) == 2); // 100
	testAssert(lowestSetBitIndex(5u) == 0); // 101
	testAssert(lowestSetBitIndex(6u) == 1); // 110
	testAssert(lowestSetBitIndex(7u) == 0); // 111
	testAssert(lowestSetBitIndex(8u) == 3); // 1000

	testAssert(lowestSetBitIndex(0x80000000u) == 31); // MSB bit set only
	testAssert(lowestSetBitIndex(0xFFFFFFFFu) == 0); // All bits set

	//===================================== lowestSetBitIndex(uint64) =====================================
	testAssert(lowestSetBitIndex((uint64)0u) == 0); // 0
	testAssert(lowestSetBitIndex((uint64)1u) == 0); // 1
	testAssert(lowestSetBitIndex((uint64)2u) == 1); // 10
	testAssert(lowestSetBitIndex((uint64)3u) == 0); // 11
	testAssert(lowestSetBitIndex((uint64)4u) == 2); // 100
	testAssert(lowestSetBitIndex((uint64)5u) == 0); // 101
	testAssert(lowestSetBitIndex((uint64)6u) == 1); // 110
	testAssert(lowestSetBitIndex((uint64)7u) == 0); // 111
	testAssert(lowestSetBitIndex((uint64)8u) == 3); // 1000

	testAssert(lowestSetBitIndex((uint64)1 << 30) == 30);
	testAssert(lowestSetBitIndex((uint64)1 << 40) == 40);
	testAssert(lowestSetBitIndex((uint64)1 << 50) == 50);
	testAssert(lowestSetBitIndex((uint64)1 << 60) == 60);

	testAssert(lowestSetBitIndex((uint64)0x8000000000000000ull) == 63); // MSB bit set only
	testAssert(lowestSetBitIndex((uint64)0xFFFFFFFFFFFFFFFFull) == 0); // All 64 bits set

	//===================================== lowestZeroBitIndex(uint32) =====================================
	testAssert(lowestZeroBitIndex(0u) == 0); // 0
	testAssert(lowestZeroBitIndex(1u) == 1); // 1
	testAssert(lowestZeroBitIndex(2u) == 0); // 10
	testAssert(lowestZeroBitIndex(3u) == 2); // 11
	testAssert(lowestZeroBitIndex(4u) == 0); // 100
	testAssert(lowestZeroBitIndex(5u) == 1); // 101
	testAssert(lowestZeroBitIndex(6u) == 0); // 110
	testAssert(lowestZeroBitIndex(7u) == 3); // 111
	testAssert(lowestZeroBitIndex(8u) == 0); // 1000

	testAssert(lowestZeroBitIndex(0xFu) == 4); // 1111
	testAssert(lowestZeroBitIndex(0xFFu) == 8); // 11111111
	testAssert(lowestZeroBitIndex(0xFFFFu) == 16);

	testAssert(lowestZeroBitIndex(0x80000000u) == 0); // Most significant bit set only.
	testAssert(lowestZeroBitIndex(0x7FFFFFFFu) == 31); // Only most significant bit set set to zero.
	testAssert(lowestZeroBitIndex(0xFFFFFFFFu) == 0); // All bits set

	//===================================== lowestZeroBitIndex(uint64) =====================================
	testAssert(lowestZeroBitIndex((uint64)0u) == 0); // 0
	testAssert(lowestZeroBitIndex((uint64)1u) == 1); // 1
	testAssert(lowestZeroBitIndex((uint64)2u) == 0); // 10
	testAssert(lowestZeroBitIndex((uint64)3u) == 2); // 11
	testAssert(lowestZeroBitIndex((uint64)4u) == 0); // 100
	testAssert(lowestZeroBitIndex((uint64)5u) == 1); // 101
	testAssert(lowestZeroBitIndex((uint64)6u) == 0); // 110
	testAssert(lowestZeroBitIndex((uint64)7u) == 3); // 111
	testAssert(lowestZeroBitIndex((uint64)8u) == 0); // 1000

	testAssert(lowestZeroBitIndex((uint64)0xFull) == 4); // 1111
	testAssert(lowestZeroBitIndex((uint64)0xFFull) == 8); // 11111111
	testAssert(lowestZeroBitIndex((uint64)0xFFFFull) == 16);
	testAssert(lowestZeroBitIndex((uint64)0xFFFFFFFFull) == 32);
	testAssert(lowestZeroBitIndex((uint64)0xFFFFFFFFFFFFull) == 48);

	testAssert(lowestZeroBitIndex((uint64)0x8000000000000000ull) == 0); // Most significant bit set only.
	testAssert(lowestZeroBitIndex((uint64)0x7FFFFFFFFFFFFFFFull) == 63); // Only most significant bit set set to zero.
	testAssert(lowestZeroBitIndex((uint64)0xFFFFFFFFFFFFFFFFull) == 0); // All bits set

	//===================================== HighestSetBitIndex =====================================
	// 32-bit highestSetBitIndex
	testAssert(highestSetBitIndex(1u) == 0); // 1
	testAssert(highestSetBitIndex(2u) == 1); // 10
	testAssert(highestSetBitIndex(3u) == 1); // 11
	testAssert(highestSetBitIndex(4u) == 2); // 100
	testAssert(highestSetBitIndex(5u) == 2); // 101
	testAssert(highestSetBitIndex(6u) == 2); // 110
	testAssert(highestSetBitIndex(7u) == 2); // 111
	testAssert(highestSetBitIndex(8u) == 3); // 1000

	testAssert(highestSetBitIndex(0xFFFFFFFFu) == 31);

	for(uint32 z=0; z<31; ++z)
		testAssert(highestSetBitIndex((uint32)1 << (uint32)z) == z);

	// 64-bit highestSetBitIndex
	testAssert(highestSetBitIndex((uint64)1ull) == 0); // 1
	testAssert(highestSetBitIndex((uint64)2ull) == 1); // 10
	testAssert(highestSetBitIndex((uint64)3ull) == 1); // 11
	testAssert(highestSetBitIndex((uint64)4ull) == 2); // 100
	testAssert(highestSetBitIndex((uint64)5ull) == 2); // 101
	testAssert(highestSetBitIndex((uint64)6ull) == 2); // 110
	testAssert(highestSetBitIndex((uint64)7ull) == 2); // 111
	testAssert(highestSetBitIndex((uint64)8ull) == 3); // 1000

	testAssert(highestSetBitIndex((uint64)0xFFFFFFFFull) == 31);
	testAssert(highestSetBitIndex((uint64)0xFFFFFFFFFFFFFFFFull) == 63); // All bits set

	for(uint32 z=0; z<63; ++z)
		testAssert(highestSetBitIndex((uint64)1 << (uint64)z) == z);

	//===================================== isBitSet =====================================
	{
		testAssert(!isBitSet(2, 1 << 0));
		testAssert( isBitSet(2, 1 << 1));
		testAssert(!isBitSet(2, 1 << 2));
		testAssert(!isBitSet(2, 1 << 3));
	}

	//===================================== areBitsSet =====================================
	{
		const uint32 bit0_flag = 1 << 0;
		const uint32 bit1_flag = 1 << 1;
		const uint32 bit2_flag = 1 << 2;

		// 0000
		testAssert(!areBitsSet(0u, bit0_flag));
		testAssert(!areBitsSet(0u, bit0_flag | bit1_flag));
		testAssert(!areBitsSet(0u, bit0_flag | bit1_flag | bit2_flag));

		// 0001
		testAssert(areBitsSet(1u, bit0_flag));
		testAssert(!areBitsSet(1u, bit0_flag | bit1_flag));
		testAssert(!areBitsSet(1u, bit1_flag));

		// 0010
		testAssert(!areBitsSet(2u, bit0_flag));
		testAssert(!areBitsSet(2u, bit0_flag | bit1_flag));
		testAssert(areBitsSet(2u, bit1_flag));

		// 0011
		testAssert(areBitsSet(3u, bit0_flag));
		testAssert(areBitsSet(3u, bit0_flag | bit1_flag));
		testAssert(!areBitsSet(3u, bit0_flag | bit1_flag | bit2_flag));
		testAssert(!areBitsSet(3u, bit1_flag | bit2_flag));

		testAssert(areBitsSet(1u << 31, 1u << 31));
	}

	//===================================== setBit =====================================
	{
		uint32 x = 0;
		setBit(x, 1u << 7);

		testAssert(x == 1u << 7);

		setBit(x, 1u << 3);

		testAssert(x == ((1u << 7) | (1u << 3)));

		setBit(x, 1u << 31);

		testAssert(x == ((1u << 7) | (1u << 3) | (1u << 31)));
	}

	//===================================== zeroBit =====================================
	{
		uint32 x = 1u << 7;
		zeroBit(x, 1u << 7);
		testAssert(x == 0);

		x = (1u << 7) | (1u << 3);
		zeroBit(x, 1u << 3);
		testAssert(x == 1u << 7);
	}

	//===================================== setOrZeroBit =====================================
	{
		uint32 x = 1u << 7;
		setOrZeroBit(x, 1u << 7, false);
		testAssert(x == 0);

		x = (1u << 7) | (1u << 3);
		setOrZeroBit(x, 1u << 3, false);
		testAssert(x == (1u << 7));

		setOrZeroBit(x, 1u << 2, true);
		testAssert(x == ((1u << 7) | (1u << 2)));
	}



	// Do a performance test of lowestSetBitIndex():
	// On Nick's Ivy Bridge i7:
	// sum: 9999985
	// elapsed: 2.2220452 cycles
	/*{
		//Timer timer;
		CycleTimer cycle_timer;

		int sum = 0;
		const int n = 10000000;
		for(int i=0; i<n; ++i)
		{
			sum += lowestSetBitIndex(i);
		}

		int64_t cycles = cycle_timer.elapsed();
		//const double elapsed = timer.elapsed();

		printVar(sum);

		//conPrint("elapsed: " + toString(1.0e9 * elapsed / n) + " ns");
		conPrint("elapsed: " + toString((float)cycles / n) + " cycles");
	}*/

}


} // end namespace BitUtils


#endif
