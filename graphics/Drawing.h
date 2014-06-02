/*=====================================================================
Drawing.h
-------------------
Copyright Glare Technologies Limited 2014 -
Generated at 2014-05-20 11:29:28 +0100
=====================================================================*/
#pragma once


#include "../maths/vec2.h"
#include "colour3.h"
class Bitmap;


/*=====================================================================
Drawing
-------------------

=====================================================================*/
namespace Drawing
{

	void drawLine(Bitmap& bitmap, const Colour3f& colour, const Vec2f& start, const Vec2f& end);


	void test();

} // end namespace Drawing
