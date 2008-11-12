/*=====================================================================
Vector.h
--------
File created by ClassTemplate on Sat Sep 02 19:47:39 2006
Code By Nicholas Chapman.
=====================================================================*/
#ifndef VECTOR_H_666
#define VECTOR_H_666


#include <assert.h>
#include "../indigo/globals.h"//TEMP
#include "stringutils.h"//TEMP
#include <malloc.h>
#include "../maths/SSE.h"


namespace js
{


/*=====================================================================
Vector
------
Similar to std::vector, but aligns the elements.
=====================================================================*/
template <class T, int alignment>
class Vector
{
public:
	inline Vector();
	inline ~Vector();

	inline Vector& operator=(const Vector& other);

	inline void reserve(unsigned int N); // Make sure capacity is at least N.
	inline void resize(unsigned int N);
	inline unsigned int capacity() const { return capacity_; }
	inline unsigned int size() const;
	inline bool empty() const;

	inline void push_back(const T& t);
	inline void pop_back();
	inline const T& back() const;
	inline T& back();
	inline T& operator[](unsigned int index);
	inline const T& operator[](unsigned int index) const;

private:
	Vector(const Vector& other);
	
	static inline void copy(const T* const src, T* dst, unsigned int num);

	T* e;
	unsigned int size_;
	unsigned int capacity_;
};


template <class T, int alignment>
Vector<T, alignment>::Vector()
:	e(0),
	size_(0),
	capacity_(0)
{
}


template <class T, int alignment>
Vector<T, alignment>::~Vector()
{
	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);

	SSE::alignedFree(e);
}


template <class T, int alignment>
Vector<T, alignment>& Vector<T, alignment>::operator=(const Vector& other)
{
	assert(capacity_ >= size_);
	
	if(this == &other)
		return *this;

	resize(other.size());
	assert(size() == other.size());
	assert(capacity_ >= size_);
	copy(other.e, e, size_);

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);

	return *this;
}


template <class T, int alignment>
void Vector<T, alignment>::reserve(unsigned int n)
{
	assert(capacity_ >= size_);

	if(n > capacity_) // If need to expand capacity
	{
		//conPrint("Vector<T>::reserve: allocing " + toString(n) + " items (" + toString(n*sizeof(T)) + " bytes)");
		
		// NOTE: bother constructing these objects?
		T* new_e = static_cast<T*>(SSE::alignedMalloc(sizeof(T) * n, alignment));
		
		if(e)
		{
			copy(e, new_e, size_); //copy existing data to new buffer

			//conPrint("Vector<T>::reserve: freeing " + toString(capacity_) + " items (" + toString(capacity_*sizeof(T)) + " bytes)");
			SSE::alignedFree(e); // Free old buffer.
		}
		e = new_e;
		capacity_ = n;
	}

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, int alignment>
void Vector<T, alignment>::resize(unsigned int n)
{
	assert(capacity_ >= size_);

	reserve(n);
	size_ = n;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, int alignment>
unsigned int Vector<T, alignment>::size() const
{
	return size_;
}


template <class T, int alignment>
bool Vector<T, alignment>::empty() const
{
	return size_ == 0;
}


template <class T, int alignment>
void Vector<T, alignment>::push_back(const T& t)
{
	assert(capacity_ >= size_);

	if(size_ >= capacity_) // If next write would exceed capacity
	{
		const unsigned int ideal_newcapacity = size_ + size_ / 2; // current size * 1.5
		const unsigned int newcapacity = ideal_newcapacity > (size_ + 1) ? ideal_newcapacity : (size_ + 1); //(size_ + 1) * 2;
		assert(newcapacity >= size_ + 1);
		reserve(newcapacity);
	}

	e[size_++] = t;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, int alignment>
void Vector<T, alignment>::pop_back()
{
	assert(capacity_ >= size_);
	assert(size_ >= 1);
	
	if(size_ >= 1)
		size_--;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, int alignment>
const T& Vector<T, alignment>::back() const
{
	assert(capacity_ >= size_);
	assert(size_ >= 1);

	return e[size_-1];
}


template <class T, int alignment>
T& Vector<T, alignment>::back()
{
	assert(size_ >= 1);
	assert(capacity_ >= size_);

	return e[size_-1];
}


template <class T, int alignment>
void Vector<T, alignment>::copy(const T * const src, T* dst, unsigned int num)
{
	assert(src);
	assert(dst);

	for(unsigned int i=0; i<num; ++i)
		dst[i] = src[i];
}


template <class T, int alignment>
T& Vector<T, alignment>::operator[](unsigned int index)
{
	assert(capacity_ >= size_);
	assert(index < size_);

	return e[index];
}


template <class T, int alignment>
const T& Vector<T, alignment>::operator[](unsigned int index) const
{
	assert(index < size_);
	assert(capacity_ >= size_);

	return e[index];
}


} //end namespace js


#endif //VECTOR_H_666
