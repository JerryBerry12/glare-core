/*=====================================================================
Vec4i.h
-------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "../maths/SSE.h"
#include <string>


/*=====================================================================
Vec4i
-----
SSE 4-vector of 32 bit signed integers.
=====================================================================*/
class Vec4i
{
public:
	INDIGO_STRONG_INLINE Vec4i() {}
	INDIGO_STRONG_INLINE explicit Vec4i(int32 x_, int32 y_, int32 z_, int32 w_) : v(_mm_set_epi32(w_, z_, y_, x_)) {}
	INDIGO_STRONG_INLINE Vec4i(__m128i v_) : v(v_) {}
	INDIGO_STRONG_INLINE explicit Vec4i(int32 x) : v(_mm_set1_epi32(x)) {}

	const std::string toString() const;

	INDIGO_STRONG_INLINE int32& operator [] (unsigned int index) { return x[index]; }
	INDIGO_STRONG_INLINE const int32& operator [] (unsigned int index) const { return x[index]; }

	INDIGO_STRONG_INLINE void operator += (const Vec4i& a);

	static void test();

	union
	{
		int32 x[4];
		__m128i v;
	};
};


void Vec4i::operator += (const Vec4i& a)
{
	v = _mm_add_epi32(v, a.v);
}


inline bool operator == (const Vec4i& a, const Vec4i& b)
{
	// NOTE: have to use _mm_cmpeq_epi32, not _mm_cmpeq_ps, since _mm_cmpeq_ps gives different results for NaN bit patterns.
	return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(a.v, b.v))) == 0xF;
}


inline bool operator != (const Vec4i& a, const Vec4i& b)
{
	return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(a.v, b.v))) != 0xF;
}


INDIGO_STRONG_INLINE const Vec4i loadVec4i(const void* const data) // SSE 2
{
	assert(((uint64)data % 16) == 0); // Must be 16-byte aligned.
	return Vec4i(_mm_load_si128((__m128i const*)data));
}

template <int index>
INDIGO_STRONG_INLINE const Vec4i copyToAll(const Vec4i& a) { return _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(a.v), _mm_castsi128_ps(a.v), _MM_SHUFFLE(index, index, index, index))); } // SSE 1

#if COMPILE_SSE4_CODE
template<int index>
INDIGO_STRONG_INLINE int elem(const Vec4i& v) { return _mm_extract_epi32(v.v, index); } // SSE 4: pextrd
#endif // COMPILE_SSE4_CODE

#if COMPILE_SSE4_CODE
INDIGO_STRONG_INLINE const Vec4i mulLo(const Vec4i& a, const Vec4i& b) { return _mm_mullo_epi32(a.v, b.v); } // // Returns lower 32 bits of 64-bit product.  SSE 4
#endif // COMPILE_SSE4_CODE
INDIGO_STRONG_INLINE const Vec4i operator + (const Vec4i& a, const Vec4i& b) { return _mm_add_epi32(a.v, b.v); } // SSE 2

INDIGO_STRONG_INLINE const Vec4i operator ^ (const Vec4i& a, const Vec4i& b) { return _mm_xor_si128(a.v, b.v); } // SSE 2
INDIGO_STRONG_INLINE const Vec4i operator & (const Vec4i& a, const Vec4i& b) { return _mm_and_si128(a.v, b.v); } // SSE 2

INDIGO_STRONG_INLINE const Vec4i operator << (const Vec4i& a, const int32 bits) { return _mm_slli_epi32(a.v, bits); } // SSE 2
INDIGO_STRONG_INLINE const Vec4i operator >> (const Vec4i& a, const int32 bits) { return _mm_srai_epi32(a.v, bits); } // SSE 2


#if COMPILE_SSE4_CODE
INDIGO_STRONG_INLINE Vec4i min(const Vec4i& a, const Vec4i& b) { return Vec4i(_mm_min_epi32(a.v, b.v)); } // SSE 4
INDIGO_STRONG_INLINE Vec4i max(const Vec4i& a, const Vec4i& b) { return Vec4i(_mm_max_epi32(a.v, b.v)); } // SSE 4

INDIGO_STRONG_INLINE Vec4i clamp(const Vec4i& v, const Vec4i& a, const Vec4i& b) { return max(a, min(v, b)); }
#endif // COMPILE_SSE4_CODE

