/*=====================================================================
ShadowMapping.cpp
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "ShadowMapping.h"


#include "OpenGLEngine.h"


ShadowMapping::ShadowMapping()
:	cur_static_depth_tex(0)
{
}


ShadowMapping::~ShadowMapping()
{
}


void ShadowMapping::init()
{
	const int base_res = 2048;
	dynamic_w = base_res;
	dynamic_h = base_res * numDynamicDepthTextures();

	static_w = base_res;
	static_h = base_res * numStaticDepthTextures();

	// Create frame buffer
	frame_buffer = new FrameBuffer();
	for(int i=0; i<2; ++i)
		static_frame_buffer[i] = new FrameBuffer();

	//blur_fb = new FrameBuffer();
	
	// Create depth tex
	//depth_tex = new OpenGLTexture();
	//depth_tex->load(w, h, NULL, NULL, 
	//	GL_DEPTH_COMPONENT16, // internal format
	//	GL_DEPTH_COMPONENT, // format
	//	GL_FLOAT, // type
	//	true // nearest filtering
	//);

	depth_tex = new OpenGLTexture(dynamic_w, dynamic_h, NULL,
		OpenGLTexture::Format_Depth_Float,
		OpenGLTexture::Filtering_Nearest // nearest filtering
	);

	cur_static_depth_tex = 0;

	for(int i=0; i<2; ++i)
		static_depth_tex[i] = new OpenGLTexture(static_w, static_h, NULL,
			OpenGLTexture::Format_Depth_Float,
			OpenGLTexture::Filtering_Nearest // nearest filtering
		);

	//col_tex = new OpenGLTexture();
	//col_tex->load(w, h, NULL, NULL, 
	//	GL_RGB32F_ARB, // internal format
	//	GL_RGB, // format
	//	GL_FLOAT, // type
	//	true // nearest filtering
	//);

	// Attach depth texture to frame buffer
	frame_buffer->bind();

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex->texture_handle, /*mipmap level=*/0);

	glDrawBuffer(GL_NONE); // No colour buffer is drawn to.
	glReadBuffer(GL_NONE); // No colour buffer is read from.

	GLenum is_complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(is_complete != GL_FRAMEBUFFER_COMPLETE)
	{
		conPrint("Error: framebuffer is not complete.");
		assert(0);
	}

	FrameBuffer::unbind();
}


void ShadowMapping::bindDepthTexFrameBufferAsTarget()
{
	frame_buffer->bind();
}


void ShadowMapping::bindStaticDepthTexFrameBufferAsTarget(int index)
{
	assert(index >= 0 && index < 2);

	static_frame_buffer[index]->bind();

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, static_depth_tex[index]->texture_handle, /*mipmap level=*/0);

	glDrawBuffer(GL_NONE); // No colour buffer is drawn to.
	glReadBuffer(GL_NONE); // No colour buffer is read from.

	GLenum is_complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(is_complete != GL_FRAMEBUFFER_COMPLETE)
	{
		conPrint("Error: static framebuffer is not complete.");
		assert(0);
	}
}


void ShadowMapping::unbindFrameBuffer()
{
	FrameBuffer::unbind();
}
