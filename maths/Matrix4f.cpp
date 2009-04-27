#include "Matrix4f.h"


#include "../indigo/TestUtils.h"
#include "matrix3.h"
#include "vec3.h"


Matrix4f::Matrix4f(const float* data)
{
	for(unsigned int i=0; i<16; ++i)
		e[i] = data[i];
}


Matrix4f::Matrix4f(const Matrix3f& upper_left_mat, const Vec3f& translation)
{
	/*
	0	4	8	12
	1	5	9	13
	2	6	10	14
	3	7	11	15
	*/

	e[0] = upper_left_mat.e[0];
	e[4] = upper_left_mat.e[1];
	e[8] = upper_left_mat.e[2];
	e[1] = upper_left_mat.e[3];
	e[5] = upper_left_mat.e[4];
	e[9] = upper_left_mat.e[5];
	e[2] = upper_left_mat.e[6];
	e[6] = upper_left_mat.e[7];
	e[10] = upper_left_mat.e[8];

	e[12] = translation.x;
	e[13] = translation.y;
	e[14] = translation.z;

	e[3] = e[7] = e[11] = 0.0f;
	e[15] = 1.0f;
}


const Matrix4f Matrix4f::identity()
{
	const float data[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	return Matrix4f(data);
}


void mul(const Matrix4f& a, const Matrix4f& b, Matrix4f& result_out)
{
	/*
	0	4	8	12
	1	5	9	13
	2	6	10	14
	3	7	11	15
	*/
	for(unsigned int outcol=0; outcol<4; ++outcol)
		for(unsigned int outrow=0; outrow<4; ++outrow)
		{
			float x = 0.0f;

			for(unsigned int i=0; i<4; ++i)
					x += a.elem(outrow, i) * b.elem(i, outcol);
		
			result_out.elem(outrow, outcol) = x;
		}
}


/*
	0	4	8	12
	1	5	9	13
	2	6	10	14
	3	7	11	15
*/
/*
	0 1 2

	3 4 5

	6 7 8
	*/
void Matrix4f::getUpperLeftMatrix(Matrix3<float>& upper_left_mat_out) const
{
	upper_left_mat_out.e[0] = e[0];
	upper_left_mat_out.e[1] = e[4];
	upper_left_mat_out.e[2] = e[8];

	upper_left_mat_out.e[3] = e[1];
	upper_left_mat_out.e[4] = e[5];
	upper_left_mat_out.e[5] = e[9];

	upper_left_mat_out.e[6] = e[2];
	upper_left_mat_out.e[7] = e[6];
	upper_left_mat_out.e[8] = e[10];
}


void Matrix4f::test()
{
	{
		const Matrix4f m = Matrix4f::identity();

		const Vec4f v(1, 2, 3, 4);

		const Vec4f res(m * v);

		testAssert(epsEqual(v, res));
	}
	{
		const float e[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

		const Matrix4f m(e);

		Matrix4f transpose;
		m.getTranspose(transpose);

		/*
		0	4	8	12
		1	5	9	13
		2	6	10	14
		3	7	11	15
		*/

		{
			const Vec4f v(1, 0, 0, 0);
			const Vec4f res(m * v);
			testAssert(epsEqual(res, Vec4f(0,1,2,3)));
		}
		{
			const Vec4f v(0, 1, 0, 0);
			const Vec4f res(m * v);
			testAssert(epsEqual(res, Vec4f(4,5,6,7)));
		}
		{
			const Vec4f v(0, 0, 1, 0);
			const Vec4f res(m * v);
			testAssert(epsEqual(res, Vec4f(8,9,10,11)));
		}
		{
			const Vec4f v(0, 0, 0, 1);
			const Vec4f res(m * v);
			testAssert(epsEqual(res, Vec4f(12,13,14,15)));
		}

		// test tranpose mult
		{
			const Vec4f v(1, 0, 0, 0);
			const Vec4f res(m.transposeMult(v));
			testAssert(epsEqual(res, Vec4f(0,4,8,12)));
		}
		{
			const Vec4f v(0, 1, 0, 0);
			const Vec4f res(m.transposeMult(v));
			testAssert(epsEqual(res, Vec4f(1,5,9,13)));
		}
		{
			const Vec4f v(0, 0, 1, 0);
			const Vec4f res(m.transposeMult(v));
			testAssert(epsEqual(res, Vec4f(2,6,10,14)));
		}
		{
			const Vec4f v(0, 0, 0, 1);
			const Vec4f res(m.transposeMult(v));
			testAssert(epsEqual(res, Vec4f(3,7,11,15)));
		}
		{
			const Vec4f v(1, 2, 3, 4);
			const Vec4f res(m.transposeMult(v));
			testAssert(epsEqual(res, Vec4f(transpose * v)));
		}
	}
	{
		// Test affine transformation
		const Matrix4f m(Matrix3f::identity(), Vec3f(1, 2, 3));

		{
			const Vec4f v(5, 6, 7, 1);
			const Vec4f res(m * v);
			testAssert(epsEqual(res, Vec4f(6, 8, 10, 1)));
		}
		{
			const Vec4f v(5, 6, 7, 0);
			const Vec4f res(m * v);
			testAssert(epsEqual(res, Vec4f(5, 6, 7, 0)));
		}
	}

	{
		// Test matrix multiplication
		const Matrix4f m = Matrix4f::identity();
		const Matrix4f m2 = Matrix4f::identity();

		Matrix4f product;
		mul(m2, m, product);
		testAssert(product == Matrix4f::identity());

		mul(m, m2, product);
		testAssert(product == Matrix4f::identity());
	}

	{
		// Test matrix multiplication
		const float e[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
		const Matrix4f m(e);

		const float e2[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
		const Matrix4f m2(e2);

		{
			const Vec4f v(5, 6, 7, 1);
			const Vec4f res(m2 * Vec4f(m * v));

			Matrix4f product;
			mul(m2, m, product);

			const Vec4f res2(product * v);

			testAssert(epsEqual(res, res2));
		}
	}

	{
		// Test translation matrix
		Matrix4f m;
		m.setToTranslationMatrix(1, 2, 3);

		{
		const Vec4f v(10, 20, 30, 1.0);
		const Vec4f res(m * v);
		
		testAssert(epsEqual(res, Vec4f(11, 22, 33, 1.0f)));
		}
		{
		const Vec4f v(10, 20, 30, 0.0);
		const Vec4f res(m * v);
		
		testAssert(epsEqual(res, Vec4f(10, 20, 30, 0.0f)));
		}
	}
}
