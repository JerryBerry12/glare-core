/*=====================================================================
Hilbert.h
---------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../maths/vec2.h"
#include <vector>


/*=====================================================================
Hilbert
-------------------
Generates a space-filling Hilbert curve.

To cover a grid with dimensons w*h,
a curve with side of dimensions roundUpToPowerOf2(max(w,h)) is needed.
THis corresponds to a depth of log_2(roundUpToPowerOf2(max(w,h))),
and will generate roundUpToPowerOf2(max(w,h))^2 indices.
=====================================================================*/
class Hilbert
{
public:
	// will generate 4^depth indices, on a square grid with sides of length 2^depth.
	static size_t getNumIndicesForDepth(int depth) { return (size_t)1 << (2*(size_t)depth); }

	// indices_out must have size == getNumIndicesForDepth(depth)
	static void generate(int depth, Vec2i* indices_out);

	static void test();
};
