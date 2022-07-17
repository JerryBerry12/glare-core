/*=====================================================================
SSBO.h
------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "BasicOpenGLTypes.h"
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Platform.h"
#include <stdlib.h>


/*=====================================================================
SSBO
----
Shader Storage Buffer Object
=====================================================================*/
class SSBO : public RefCounted
{
public:
	SSBO();
	~SSBO();

	void bind();
	void unbind();

	void allocate(size_t size_B);

	void updateData(size_t dest_offset, const void* src_data, size_t src_size);

	GLuint handle;

private:
	GLARE_DISABLE_COPY(SSBO)

	size_t allocated_size;
};


typedef Reference<SSBO> SSBORef;