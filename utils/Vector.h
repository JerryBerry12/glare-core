/*=====================================================================
Vector.h
--------
Copyright Glare Technologies Limited 2013 -
File created by ClassTemplate on Sat Sep 02 19:47:39 2006
=====================================================================*/
#pragma once


#include "../maths/SSE.h"
#include "../maths/mathstypes.h"
#include <assert.h>
#include <memory>
#include <new>


// If this is defined, prints out messages on allocing and deallocing in reserve()
//#define JS_VECTOR_VERBOSE 1


#if JS_VECTOR_VERBOSE
#include "../indigo/globals.h"
#include "StringUtils.h"
#include <typeinfo.h>
#endif


namespace js
{


/*=====================================================================
Vector
------
Similar to std::vector, but aligns the elements.
Has some slightly different semantics:
Doesn't do default (zero) initialisation of POD types.
Unit tests for this class are in VectorUnitTests.cpp
=====================================================================*/
template <class T, size_t alignment>
class Vector
{
public:
	inline Vector(); // Initialise as an empty vector.
	inline Vector(size_t count); // Initialise with default initialisation.
	inline Vector(size_t count, const T& val); // Initialise with count copies of val.
	inline Vector(const Vector& other); // Initialise as a copy of other
	inline ~Vector();

	inline Vector& operator=(const Vector& other);

	inline void reserve(size_t N); // Make sure capacity is at least N.
	inline void resize(size_t N); // Resize to size N, using default constructor if N > size().
	inline void resize(size_t N, const T& val); // Resize to size N, using copies of val if N > size().
	inline void resizeUninitialised(size_t N); // Resize to size N, but don't destroy or construct objects.
	inline size_t capacity() const { return capacity_; }
	inline size_t size() const;
	inline bool empty() const;
	inline void clear(); // Resize to size zero, does not free mem.
	inline void clearAndFreeMem(); // Set size to zero, but also frees actual array memory.

	inline void clearAndSetCapacity(size_t N);

	inline void push_back(const T& t);
	inline void push_back_uninitialised();
	inline void pop_back();
	inline const T& back() const;
	inline T& back();
	inline T& operator[](size_t index);
	inline const T& operator[](size_t index) const;

	inline bool operator==(const Vector& other) const;
	inline bool operator!=(const Vector& other) const;

	typedef T* iterator;
	typedef const T* const_iterator;

	inline iterator begin();
	inline iterator end();
	inline const_iterator begin() const;
	inline const_iterator end() const;

private:
	T* e; // Elements
	size_t size_; // Number of elements in the vector.  Elements e[0] to e[size_-1] are proper constructed objects.
	size_t capacity_; // This is the number of elements we have roon for in e.
};


template <class T, size_t alignment>
Vector<T, alignment>::Vector()
:	e(0),
	size_(0),
	capacity_(0)
{
	//assert(alignment > sizeof(T) || sizeof(T) % alignment == 0); // sizeof(T) needs to be a multiple of alignment, otherwise e[1] will be unaligned.
}


template <class T, size_t alignment>
Vector<T, alignment>::Vector(size_t count)
:	e(0),
	size_(count),
	capacity_(count)
{
	assert(alignment > sizeof(T) || sizeof(T) % alignment == 0); // sizeof(T) needs to be a multiple of alignment, otherwise e[1] will be unaligned.

	if(count > 0)
	{
		// Allocate new memory
		e = static_cast<T*>(SSE::alignedMalloc(sizeof(T) * count, alignment));

		// Initialise elements
		// NOTE: We use the constructor form without parentheses, in order to avoid default (zero) initialisation of POD types. 
		// See http://stackoverflow.com/questions/620137/do-the-parentheses-after-the-type-name-make-a-difference-with-new for more info.
		for(T* elem=e; elem<e + count; ++elem)
			::new (elem) T;
	}

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
Vector<T, alignment>::Vector(size_t count, const T& val)
:	e(0),
	size_(count),
	capacity_(count)
{
	assert(alignment > sizeof(T) || sizeof(T) % alignment == 0); // sizeof(T) needs to be a multiple of alignment, otherwise e[1] will be unaligned.

	if(count > 0)
	{
		// Allocate new memory
		e = static_cast<T*>(SSE::alignedMalloc(sizeof(T) * count, alignment));
		
		// Initialise elements as a copy of val.
		for(T* elem=e; elem<e + count; ++elem)
			::new (elem) T(val);
	}

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
Vector<T, alignment>::Vector(const Vector<T, alignment>& other)
{
	// Allocate new memory
	e = static_cast<T*>(SSE::alignedMalloc(sizeof(T) * other.size_, alignment));

	// Copy-construct new objects from existing objects in 'other'.
	std::uninitialized_copy(other.e, other.e + other.size_, 
		e // dest
	);
		
	size_ = other.size_;
	capacity_ = other.size_;
}


template <class T, size_t alignment>
Vector<T, alignment>::~Vector()
{
	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);

	// Destroy objects
	for(size_t i=0; i<size_; ++i)
		e[i].~T();

	SSE::alignedFree(e);
}


template <class T, size_t alignment>
Vector<T, alignment>& Vector<T, alignment>::operator=(const Vector& other)
{
	assert(capacity_ >= size_);

	if(this == &other)
		return *this;

	/*
	if we already have the capacity that we need, just copy objects over.

	otherwise free existing mem, alloc mem, copy elems over
	*/

	if(capacity_ >= other.size_)
	{
		// Destroy existing objects
		for(size_t i=0; i<size_; ++i)
			e[i].~T();

		// Copy elements over from other
		if(e) 
			std::uninitialized_copy(other.e, other.e + other.size_, e);

		size_ = other.size_;
	}
	else
	{
		// Destroy existing objects
		for(size_t i=0; i<size_; ++i)
			e[i].~T();

		// Free existing mem
		SSE::alignedFree(e);

		// Allocate new memory
		e = static_cast<T*>(SSE::alignedMalloc(sizeof(T) * other.size_, alignment));

		// Copy elements over from other
		if(e)
			std::uninitialized_copy(other.e, other.e + other.size_, e);

		size_ = other.size_;
		capacity_ = other.size_;
	}

	assert(size() == other.size());


	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);

	return *this;
}


template <class T, size_t alignment>
void Vector<T, alignment>::reserve(size_t n)
{
	assert(capacity_ >= size_);

	if(n > capacity_) // If need to expand capacity
	{
		#if JS_VECTOR_VERBOSE
		conPrint("Vector<" + std::string(typeid(T).name()) + ", " + toString(alignment) + ">::reserve: allocing " + toString(n) + " items (" + toString(n*sizeof(T)) + " bytes)");
		#endif
		
		// Allocate new memory
		T* new_e = static_cast<T*>(SSE::alignedMalloc(sizeof(T) * n, alignment));

		// Copy-construct new objects from existing objects.
		// e[0] to e[size_-1] will now be proper initialised objects.
		std::uninitialized_copy(e, e + size_, new_e);
		
		if(e)
		{
			#if JS_VECTOR_VERBOSE
			conPrint("Vector<" + std::string(typeid(T).name()) + ", " + toString(alignment) + ">::reserve: freeing " + toString(capacity_) + " items (" + toString(capacity_*sizeof(T)) + " bytes)");
			#endif

			// Destroy old objects
			for(size_t i=0; i<size_; ++i)
				e[i].~T();

			SSE::alignedFree(e); // Free old buffer.
		}
		e = new_e;
		capacity_ = n;
	}

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
void Vector<T, alignment>::resize(size_t n, const T& val)
{
	assert(capacity_ >= size_);

	if(n > size_)
	{
		if(n > capacity_)
		{
			const size_t newcapacity = myMax(n, 2 * capacity_);
			reserve(newcapacity);
		}

		// Initialise elements e[size_] to e[n-1] as a copy of val.
		//std::uninitialized_fill(e + size_, e + n, val);
		for(T* elem=e + size_; elem<e + n; ++elem)
		{
			::new (elem) T(val);
		}
	}
	else if(n < size_)
	{
		// Destroy elements e[n] to e[size-1]
		for(size_t i=n; i<size_; ++i)
			e[i].~T();
	}

	size_ = n;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
void Vector<T, alignment>::resize(size_t n)
{
	assert(capacity_ >= size_);

	if(n > size_)
	{
		if(n > capacity_)
		{
			const size_t newcapacity = myMax(n, 2 * capacity_);
			reserve(newcapacity);
		}

		// Initialise elements e[size_] to e[n-1]
		// NOTE: We use the constructor form without parentheses, in order to avoid default (zero) initialisation of POD types. 
		// See http://stackoverflow.com/questions/620137/do-the-parentheses-after-the-type-name-make-a-difference-with-new for more info.
		for(T* elem=e + size_; elem<e + n; ++elem)
		{
			::new (elem) T;
		}
	}
	else if(n < size_)
	{
		// Destroy elements e[n] to e[size-1]
		for(size_t i=n; i<size_; ++i)
			e[i].~T();
	}

	size_ = n;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
void Vector<T, alignment>::resizeUninitialised(size_t n)
{
	assert(capacity_ >= size_);

	if(n > capacity_)
	{
		const size_t newcapacity = myMax(n, 2 * capacity_);
		reserve(newcapacity);
	}

	size_ = n;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
size_t Vector<T, alignment>::size() const
{
	return size_;
}


template <class T, size_t alignment>
bool Vector<T, alignment>::empty() const
{
	return size_ == 0;
}


template <class T, size_t alignment>
void Vector<T, alignment>::clear()
{
	// Destroy objects
	for(size_t i=0; i<size_; ++i)
		e[i].~T();

	size_ = 0;
}


template <class T, size_t alignment>
void Vector<T, alignment>::clearAndFreeMem() // Set size to zero, but also frees actual array memory.
{
	// Destroy objects
	for(size_t i=0; i<size_; ++i)
		e[i].~T();

	SSE::alignedFree(e); // Free old buffer.
	e = NULL;
	size_ = 0;
	capacity_ = 0;
}


template <class T, size_t alignment>
inline void Vector<T, alignment>::clearAndSetCapacity(size_t N)
{
	clearAndFreeMem();
	reserve(N);
}


template <class T, size_t alignment>
void Vector<T, alignment>::push_back(const T& t)
{
	assert(capacity_ >= size_);

	// Check to see if we are out of capacity:
	if(size_ == capacity_)
	{
		const size_t newcapacity = myMax(size_ + 1, 2 * capacity_);
		reserve(newcapacity);
	}

	// Construct e[size_] from t
	::new ((e + size_)) T(t);

	size_++;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
void Vector<T, alignment>::push_back_uninitialised()
{
	assert(capacity_ >= size_);

	// Check to see if we are out of capacity:
	if(size_ == capacity_)
	{
		const size_t newcapacity = myMax(size_ + 1, 2 * capacity_);
		reserve(newcapacity);
	}

	// Don't construct e[size_] !

	size_++;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
void Vector<T, alignment>::pop_back()
{
	assert(capacity_ >= size_);
	assert(size_ >= 1);

	// It's undefined to pop_back() on an empty vector.
	
	// Destroy object
	e[size_ - 1].~T();
	size_--;

	assert(capacity_ >= size_);
	assert(size_ > 0 ? (e != NULL) : true);
}


template <class T, size_t alignment>
const T& Vector<T, alignment>::back() const
{
	assert(capacity_ >= size_);
	assert(size_ >= 1);

	return e[size_-1];
}


template <class T, size_t alignment>
T& Vector<T, alignment>::back()
{
	assert(size_ >= 1);
	assert(capacity_ >= size_);

	return e[size_-1];
}


template <class T, size_t alignment>
T& Vector<T, alignment>::operator[](size_t index)
{
	assert(capacity_ >= size_);
	assert(index < size_);

	return e[index];
}


template <class T, size_t alignment>
const T& Vector<T, alignment>::operator[](size_t index) const
{
	assert(index < size_);
	assert(capacity_ >= size_);

	return e[index];
}


template <class T, size_t alignment>
bool Vector<T, alignment>::operator==(const Vector<T, alignment>& other) const
{
	assert(index < size_);
	assert(capacity_ >= size_);

	if(size() != other.size())
		return false;

	for(size_t i=0; i<size_; ++i)
		if(e[i] != other[i])
			return false;

	return true;
}


template <class T, size_t alignment>
bool Vector<T, alignment>::operator!=(const Vector<T, alignment>& other) const
{
	assert(index < size_);
	assert(capacity_ >= size_);

	if(size() != other.size())
		return true;

	for(size_t i=0; i<size_; ++i)
		if(e[i] != other[i])
			return true;

	return false;
}


template <class T, size_t alignment>
typename Vector<T, alignment>::iterator Vector<T, alignment>::begin()
{
	return e;
}


template <class T, size_t alignment>
typename Vector<T, alignment>::iterator Vector<T, alignment>::end()
{
	return e + size_;
}


template <class T, size_t alignment>
typename Vector<T, alignment>::const_iterator Vector<T, alignment>::begin() const
{
	return e;
}


template <class T, size_t alignment>
typename Vector<T, alignment>::const_iterator Vector<T, alignment>::end() const
{
	return e + size_;
}


} //end namespace js
