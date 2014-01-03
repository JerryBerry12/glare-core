/*=====================================================================
InterpolatedTable1D.h
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Thu Oct 07 22:28:38 +1300 2010
=====================================================================*/
#pragma once


#include "../maths/mathstypes.h"
#include <vector>


/*=====================================================================
InterpolatedTable1D
-------------------
data.size() must be < 2^31
=====================================================================*/
template <class Real, class Datum>
class InterpolatedTable1D
{
public:
	inline InterpolatedTable1D(){}
	inline InterpolatedTable1D(
		const std::vector<Datum>& data,
		Real start_x,
		Real end_x
	);
	inline ~InterpolatedTable1D(){}

	inline void init(const std::vector<Datum>& data,
		Real start_x,
		Real end_x
	);


	inline Datum getValue(Real x) const;

private:
	std::vector<Datum> data;
	int data_size;
	Real start_x;
	Real end_x;
	Real recip_gap_width;
};


template <class Real, class Datum>
InterpolatedTable1D<Real, Datum>::InterpolatedTable1D(
		const std::vector<Datum>& data_,
		Real start_x_,
		Real end_x_
)
{
	init(data_, start_x_, end_x_);
}


template <class Real, class Datum>
void InterpolatedTable1D<Real, Datum>::init(
		const std::vector<Datum>& data_,
		Real start_x_,
		Real end_x_
)
{
	data = data_;
	start_x = start_x_;
	end_x = end_x_;
	data_size = (int)data_.size();

	assert(data.size() >= 2);
	assert(end_x > start_x);
	recip_gap_width = (Real)(data.size() - 1) / (end_x - start_x);
}


template <class Real, class Datum>
Datum InterpolatedTable1D<Real, Datum>::getValue(Real x) const
{
	const float offset = (x - start_x) * recip_gap_width;
	const int index = (int)offset;

	if(offset < 0) // In the case where x < start_x, offset can be e.g. -0.1, which will be truncated to index = 0.  In this case we just want to return data[0].
		return data[0];
	if(index + 1 >= data_size)
		return data[data_size - 1];

	const float t = offset - index;
	assert(t >= 0 && t <= 1);

	return Maths::uncheckedLerp(data[index], data[index+1], t);
}
