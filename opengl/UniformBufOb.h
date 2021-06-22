/*=====================================================================
UniformBufOb.h
--------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "IncludeOpenGL.h"
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Platform.h"
#include <vector>



/*=====================================================================
UniformBufOb
------------
Vertex array object
=====================================================================*/
class UniformBufOb : public RefCounted
{
public:
	UniformBufOb();
	~UniformBufOb();

	void bind();
	void unbind();

	void allocate(size_t size_B);

	void updateData(size_t dest_offset, const void* src_data, size_t src_size);

	GLuint handle;

private:
	GLARE_DISABLE_COPY(UniformBufOb)
};


typedef Reference<UniformBufOb> UniformBufObRef;