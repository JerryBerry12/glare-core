/*=====================================================================
FrameBuffer.cpp
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "FrameBuffer.h"


FrameBuffer::FrameBuffer()
:	buffer_name(0),
	xres(0),
	yres(0)
{
	// Create new frame buffer
	glGenFramebuffers(1, &buffer_name);
}


FrameBuffer::~FrameBuffer()
{
	glDeleteFramebuffers(1, &buffer_name);
}


void FrameBuffer::bind()
{
	// Make buffer active
	glBindFramebuffer(GL_FRAMEBUFFER, buffer_name);
}


void FrameBuffer::unbind()
{
	// Unbind buffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void FrameBuffer::bindTextureAsTarget(OpenGLTexture& tex, GLenum attachment_point)
{
	xres = tex.xRes();
	yres = tex.yRes();

	bind(); // Bind this frame buffer

	// Bind the texture
	glFramebufferTexture(GL_FRAMEBUFFER, // target
		attachment_point,
		tex.texture_handle, // texture
		0); // mipmap level
}
