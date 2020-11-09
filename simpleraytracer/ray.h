/*=====================================================================
ray.h
-----
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "../maths/Vec4f.h"


/*=====================================================================
Ray
---
Need not be normalised.
=====================================================================*/
class Ray
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	INDIGO_STRONG_INLINE Ray() {}

	INDIGO_STRONG_INLINE Ray(const Vec4f& startpos_, const Vec4f& unitdir_, float min_t_, float max_t_, bool shadow_trace_ = true)
	:	startpos_f(startpos_),
		unitdir_f(unitdir_),
		min_t(min_t_),
		max_t(max_t_),
		shadow_trace(shadow_trace_)
	{
		assert(epsEqual(startpos_.x[3], 1.0f));
		assert(epsEqual(unitdir_.x[3], 0.0f));
		assert(SSE::isSSEAligned(this));
		assert(max_t_ >= min_t_);

		// Compute reciprocal ray direction, but avoid infinities. 
		// If any of the components of the reciprocal direction are -INF or +INF, replace with numeric_limits<float>::max().
		// This code handles denormalised values properly.
		const Vec4f raw_recip_dir = div(Vec4f(1.f), unitdir_);
		this->recip_unitdir_f = select(Vec4f(std::numeric_limits<float>::max()), raw_recip_dir,
			parallelOr(
				parallelEq(raw_recip_dir, Vec4f(std::numeric_limits<float>::infinity())),
				parallelEq(raw_recip_dir, Vec4f(-std::numeric_limits<float>::infinity()))
			)
		);
	}

	INDIGO_STRONG_INLINE const Vec4f& startPos() const { return startpos_f; }
	INDIGO_STRONG_INLINE const Vec4f& unitDir() const { return unitdir_f; }

	INDIGO_STRONG_INLINE const Vec4f& startPosF() const { return startpos_f; }
	INDIGO_STRONG_INLINE const Vec4f& unitDirF() const { return unitdir_f; }

	INDIGO_STRONG_INLINE const Vec4f& getRecipRayDirF() const { return recip_unitdir_f; }

	INDIGO_STRONG_INLINE const Vec4f pointf(const float t) const { return startpos_f + (unitdir_f * t); }

	INDIGO_STRONG_INLINE static float computeMinT(float origin_error, float cos_theta)
	{
		return 4 * origin_error * myMin(20.0f, 1 / cos_theta);
	}

	INDIGO_STRONG_INLINE float minT() const { return min_t; }
	INDIGO_STRONG_INLINE float maxT() const { return max_t; }


	INDIGO_STRONG_INLINE void setStartPos(const Vec4f& s) { startpos_f = s; }
	INDIGO_STRONG_INLINE void setMinT(float min_t_) { min_t = min_t_; }
	INDIGO_STRONG_INLINE void setMaxT(float max_t_) { max_t = max_t_; }

	static void test();

//private:
	Vec4f startpos_f;
	Vec4f unitdir_f;
	Vec4f recip_unitdir_f;
	float min_t;
	float max_t;
public:
	bool shadow_trace;
};
