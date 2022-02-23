/*=====================================================================
VBO.h
-----
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "IncludeOpenGL.h"
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Platform.h"


/*=====================================================================
VBO
-------------------

=====================================================================*/
class VBO : public RefCounted
{
public:
	// data can be NULL
	VBO(const void* data, size_t size, GLenum buffer_type = GL_ARRAY_BUFFER, GLenum usage = GL_STATIC_DRAW);
	~VBO();


	void updateData(const void* data, size_t data_size); // data_size must be <= size

	void bind();
	void unbind();

	const GLuint bufferName() const { return buffer_name; }
	const size_t getSize() const{ return size; }
private:
	GLARE_DISABLE_COPY(VBO)

	GLuint buffer_name;
	GLenum buffer_type;
	size_t size; // in bytes
};


typedef Reference<VBO> VBORef;
