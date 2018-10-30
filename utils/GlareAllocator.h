/*=====================================================================
GlareAllocator.h
----------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "ThreadSafeRefCounted.h"
#include <cstring> // for size_t


namespace glare
{


class Allocator : public ThreadSafeRefCounted
{
public:
	virtual ~Allocator() {}
	virtual void* alloc(size_t size, size_t alignment) = 0;
	virtual void free(void* ptr) = 0;
};


} // End namespace glare
