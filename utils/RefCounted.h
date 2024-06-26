/*=====================================================================
RefCounted.h
------------
Copyright Glare Technologies Limited 2021 - 
=====================================================================*/
// Not using pragma once since we copy this file and pragma once is path based
#ifndef GLARE_REFCOUNTED_H
#define GLARE_REFCOUNTED_H


#include "Platform.h"
#include <cassert>


///
/// This is a 'mixin' class that adds a refcount and a few methods to increment and decrement the ref count etc..
/// Derive from this to make a class reference-counted.
///
class RefCounted
{
public:
	RefCounted() : refcount(0) {}

	// We don't want a virtual destructor in this class as we don't want to force derived classes to be polymorphic (e.g. to require a vtable).
	~RefCounted()
	{
		assert(refcount == 0);
	}

	/// Increment reference count
	inline void incRefCount() const
	{ 
		assert(refcount >= 0);
		refcount++; 
	}

	/// Returns previous reference count
	inline int64 decRefCount() const
	{ 
		const int64 prev_ref_count = refcount;
		refcount--;
		assert(refcount >= 0);
		return prev_ref_count;
	}

	inline int64 getRefCount() const
	{ 
		assert(refcount >= 0);
		return refcount; 
	}
	
private:
	// Classes inheriting from RefCounted shouldn't rely on the default copy constructor or assigment operator, 
	// as these may cause memory leaks when refcount is copied directly.
	GLARE_DISABLE_COPY(RefCounted)

	mutable int64 refcount;
};

#endif // GLARE_REFCOUNTED_H
