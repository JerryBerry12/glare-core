/*=====================================================================
Vec4.h
------
File created by ClassTemplate on Thu Mar 26 15:28:20 2009
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __VEC4_H_666_
#define __VEC4_H_666_


#include "SSE.h"
#include "mathstypes.h"
#include <assert.h>
#include <string>
#include "../utils/platform.h"


/*=====================================================================
Vec4
----

=====================================================================*/
SSE_ALIGN class SSE_ALIGN_SUFFIX Vec4f
{
public:
	typedef float RealType;


	INDIGO_STRONG_INLINE Vec4f() {}
	INDIGO_STRONG_INLINE explicit Vec4f(float x_, float y_, float z_, float w_) { x[0] = x_; x[1] = y_; x[2] = z_; x[3] = w_; }
	INDIGO_STRONG_INLINE Vec4f(__m128 v_) : v(v_) {}
	INDIGO_STRONG_INLINE explicit Vec4f(float f) { x[0] = f, x[1] = f, x[2] = f, x[3] = f; }

	INDIGO_STRONG_INLINE ~Vec4f() {}

	INDIGO_STRONG_INLINE void set(float x_, float y_, float z_, float w_) { x[0] = x_; x[1] = y_; x[2] = z_; x[3] = w_; }

	INDIGO_STRONG_INLINE Vec4f& operator = (const Vec4f& a);

	INDIGO_STRONG_INLINE float& operator [] (unsigned int index);
	INDIGO_STRONG_INLINE const float& operator [] (unsigned int index) const;

	INDIGO_STRONG_INLINE void operator += (const Vec4f& a);
	INDIGO_STRONG_INLINE void operator -= (const Vec4f& a);

	INDIGO_STRONG_INLINE void operator *= (float f);

	inline bool operator == (const Vec4f& a) const;


	INDIGO_STRONG_INLINE float length() const;
	INDIGO_STRONG_INLINE float length2() const;
	INDIGO_STRONG_INLINE float getDist(const Vec4f& a) const;
	INDIGO_STRONG_INLINE float getDist2(const Vec4f& a) const;

	inline bool isUnitLength() const;

	const std::string toString() const;


	static void test();


	union
	{
		float x[4];
		__m128 v;
	};
};


// Some stand-alone functions that operate on Vec4fs


// NOTE: This returns __m128 instead of 'const Vec4f', because VC9 won't inline the function if it returns 'const Vec4f'.
INDIGO_STRONG_INLINE __m128 operator + (const Vec4f& a, const Vec4f& b)
{
	return _mm_add_ps(a.v, b.v);
}


INDIGO_STRONG_INLINE __m128 operator - (const Vec4f& a, const Vec4f& b)
{
	return _mm_sub_ps(a.v, b.v);
}


INDIGO_STRONG_INLINE __m128 operator * (const Vec4f& a, float f)
{
	return _mm_mul_ps(a.v, _mm_load_ps1(&f));
}


INDIGO_STRONG_INLINE float dot(const Vec4f& a, const Vec4f& b)
{
	const __m128 prod = _mm_mul_ps(a.v, b.v); // [w, z, y, x]

	const __m128 s = _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(3, 3, 1, 1)); // [w, w, y, y]

	const __m128 sum = _mm_add_ps(prod, s); // [2w, z+w, 2y, x+y]

	const __m128 s2 = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(2, 2, 2, 2)); // [z+w, z+w, z+w, z+w]

	const SSE_ALIGN Vec4f res(_mm_add_ps(sum, s2)); // [x+y+z+w, ...]

	return res.x[0];
}


INDIGO_STRONG_INLINE __m128 crossProduct(const Vec4f& a, const Vec4f& b)
{
	/*
		want (aybz - azby, azbx - axbz, axby - aybx, 0.0f)

		= [0, axby - aybx, azbx - axbz, aybz - azby]
	*/
	return
		_mm_sub_ps(
			_mm_mul_ps(
				_mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(3, 0, 2, 1)), // [0, ax, az, ay]
				_mm_shuffle_ps(b.v, b.v, _MM_SHUFFLE(3, 1, 0, 2)) // [0, by, bx, bz]
				), // [0, axby, azbx, aybz]
			_mm_mul_ps(
				_mm_shuffle_ps(a.v, a.v, _MM_SHUFFLE(3, 1, 0, 2)), // [0, ay, ax, az]
				_mm_shuffle_ps(b.v, b.v, _MM_SHUFFLE(3, 0, 2, 1)) // [0, bx, bz, by]
				) // [0, aybx, axbz, azby]
			)
		;
}


inline bool epsEqual(const Vec4f& a, const Vec4f& b)
{
	return ::epsEqual(a.x[0], b.x[0]) &&
		::epsEqual(a.x[1], b.x[1]) &&
		::epsEqual(a.x[2], b.x[2]) &&
		::epsEqual(a.x[3], b.x[3]);
}


INDIGO_STRONG_INLINE __m128 normalise(const Vec4f& a)
{
	return a * (1.0f / a.length());
}


INDIGO_STRONG_INLINE __m128 normalise(const Vec4f& a, float& length_out)
{
	length_out = a.length();

	return a * (1.0f / length_out);
}


INDIGO_STRONG_INLINE __m128 maskWToZero(const Vec4f& a)
{
	const SSE_ALIGN unsigned int mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };
	return _mm_and_ps(a.v, _mm_load_ps((const float*)mask));
}


Vec4f& Vec4f::operator = (const Vec4f& a)
{
	v = a.v;
	return *this;
}


float& Vec4f::operator [] (unsigned int index)
{
	assert(index < 4);
	return x[index];
}


const float& Vec4f::operator [] (unsigned int index) const
{
	assert(index < 4);
	return x[index];
}


void Vec4f::operator += (const Vec4f& a)
{
	v = _mm_add_ps(v, a.v);
}


void Vec4f::operator -= (const Vec4f& a)
{
	v = _mm_add_ps(v, a.v);
}


void Vec4f::operator *= (float f)
{
	v = _mm_mul_ps(v, _mm_load_ps1(&f));
}


bool Vec4f::operator == (const Vec4f& a) const
{
	// NOTE: could speed this up with an SSE instruction, but does it need to be fast?
	// Exact floating point comparison should be rare.
	return
		x[0] == a.x[0] &&
		x[1] == a.x[1] &&
		x[2] == a.x[2] &&
		x[3] == a.x[3];
}


float Vec4f::length() const
{
	return std::sqrt(dot(*this, *this));
}


float Vec4f::length2() const
{
	return dot(*this, *this);
}


float Vec4f::getDist(const Vec4f& a) const
{
	return Vec4f(a - *this).length();
}


float Vec4f::getDist2(const Vec4f& a) const
{
	return Vec4f(a - *this).length2();
}


bool Vec4f::isUnitLength() const
{
	return ::epsEqual(1.0f, length());
}


#endif //__VEC4_H_666_
