/*=====================================================================
FrameBuffer.h
-------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include <QtOpenGL/QGLWidget>
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include <string>
class OpenGLShader;


/*=====================================================================
FrameBuffer
---------

=====================================================================*/
class FrameBuffer : public RefCounted
{
public:
	FrameBuffer();
	~FrameBuffer();

	void bind();
	void unbind();

private:
	INDIGO_DISABLE_COPY(FrameBuffer);
public:
	
	GLuint buffer_name;
};
