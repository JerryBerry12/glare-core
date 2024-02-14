/*=====================================================================
RenderBuffer.h
--------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "BasicOpenGLTypes.h"
#include "OpenGLTexture.h"
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"


/*=====================================================================
RenderBuffer
------------

=====================================================================*/
class RenderBuffer : public RefCounted
{
public:
	RenderBuffer(size_t tex_xres, size_t tex_yres, int MSAA_samples, OpenGLTexture::Format format);
	~RenderBuffer();

	void bind();

	static void unbind();

	
	size_t xRes() const { return xres; }
	size_t yRes() const { return yres; }
private:
	GLARE_DISABLE_COPY(RenderBuffer);
public:
	
	GLuint buffer_name;
	size_t xres, yres;
};


typedef Reference<RenderBuffer> RenderBufferRef;
