/*=====================================================================
basis.h
-------
File created by ClassTemplate on Tue Nov 26 01:17:46 2002
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __BASIS_H_666_
#define __BASIS_H_666_



#include "matrix3.h"
#include "vec3.h"


/*=====================================================================
Basis
-----
An orthonormal basis.
The axis vectors i, j, k are the column vectors of mat.
=====================================================================*/
template <class Real>
class Basis
{
public:
	/*=====================================================================
	Basis
	-----
	
	=====================================================================*/
	Basis();
	inline Basis(const Basis& other);
	inline Basis(const Matrix3<Real>& mat);

	~Basis();

	inline Basis& operator = (const Basis& other);

	inline void init();

	inline const Vec3<Real> i() const;
	inline const Vec3<Real> j() const;
	inline const Vec3<Real> k() const;


	inline const Vec3<Real> transformVectorToLocal(const Vec3<Real>& v) const;
	inline const Vec3<Real> transformVectorToParent(const Vec3<Real>& v) const;

	/*================================================================================
	rotAroundAxis
	-------------
	rotates the matrix around the axis specified.

	The rotation is counter-clockwise when looking along the axis towards the origin.  
	All rotation angles are in radians.

	The specified rotation axis should have length 1
	================================================================================*/
	void rotAroundAxis(const Vec3<Real>& axis, float angle);


	const Basis getThisBasisWRTParent(const Basis& parent) const;

	inline void invert();

	inline const Matrix3<Real>& getMatrix() const;
	inline Matrix3<Real>& getMatrix();


	/*================================================================================
	constructFromVector
	-------------------
	Forms two other axes for the vector, creating an orthonormal basis.
	================================================================================*/
	void constructFromVector(const Vec3<Real>& vec);

private:
	Matrix3<Real> mat;
};

template <class Real>
inline Basis<Real>::Basis(const Basis& other)
:	mat(other.mat)
{}

template <class Real>
inline Basis<Real>::Basis(const Matrix3<Real>& mat_)
:	mat(mat_)
{}


template <class Real>
inline Basis<Real>& Basis<Real>::operator = (const Basis<Real>& other)
{
	mat = other.mat;
	return *this;
}

template <class Real>
void Basis<Real>::init()
{
	mat.init();
}


template <class Real>
const Vec3<Real> Basis<Real>::i() const
{
	return mat.getColumn0();
}

template <class Real>
const Vec3<Real> Basis<Real>::j() const
{
	return mat.getColumn1();
}

template <class Real>
const Vec3<Real> Basis<Real>::k() const
{
	return mat.getColumn2();
}

template <class Real>
const Vec3<Real> Basis<Real>::transformVectorToLocal(const Vec3<Real>& v) const
{
	return Vec3<Real>( dot(i(), v), dot(j(), v), dot(k(), v) );
}


template <class Real>
const Vec3<Real> Basis<Real>::transformVectorToParent(const Vec3<Real>& v) const
{
	//NOTE: assuming orthonormal basis here
	return (i() * v.x) + (j() * v.y) + (k() * v.z);
}


/*inline void Basis::invert()
{
	mat.invert();
}*/


template <class Real>
inline const Matrix3<Real>& Basis<Real>::getMatrix() const
{
	return mat;
}

template <class Real>
inline Matrix3<Real>& Basis<Real>::getMatrix()
{
	return mat;
}

template <class Real>
Basis<Real>::Basis()
{
	mat.init();
}

template <class Real>
Basis<Real>::~Basis()
{
	
}

/*

template <class Real>
void Basis<Real>::rotAroundAxis(const Vec3<Real>& axis, Real angle)
{
	mat.rotAroundAxis(axis, angle);
}*/

template <class Real>
const Basis<Real> Basis<Real>::getThisBasisWRTParent(const Basis& parent) const
{
	return Basis<Real>(getMatrix() * parent.getMatrix());
}

template <class Real>
void Basis<Real>::constructFromVector(const Vec3<Real>& vec)
{
	assert(::epsEqual(vec.length(), (Real)1.0));

	Vec3<Real> v2;//x axis

	//thanks to Pharr and Humprehys for this code
	if(fabs(vec.x) > fabs(vec.y))
	{
		const Real recip_len = 1.0 / sqrt(vec.x * vec.x + vec.z * vec.z);

		v2.set(-vec.z * recip_len, 0.0f, vec.x * recip_len);
	}
	else
	{
		const Real recip_len = 1.0 / sqrt(vec.y * vec.y + vec.z * vec.z);

		v2.set(0.0, vec.z * recip_len, -vec.y * recip_len);
	}

	assert(::epsEqual(v2.length(), 1.0));

	mat.setColumn0(v2);
	mat.setColumn1(::crossProduct(vec, v2));
	mat.setColumn2(vec);
	
	assert(::epsEqual(dot(mat.getColumn0(), mat.getColumn1()), 0.0));
	assert(::epsEqual(dot(mat.getColumn0(), mat.getColumn2()), 0.0));
	assert(::epsEqual(dot(mat.getColumn1(), mat.getColumn2()), 0.0));
}

typedef Basis<float> Basisf;
typedef Basis<double> Basisd;

#endif //__BASIS_H_666_




