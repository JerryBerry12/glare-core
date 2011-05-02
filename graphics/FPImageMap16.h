/*=====================================================================
FPImageMap16.h
--------------
File created by ClassTemplate on Sun May 18 21:48:49 2008
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __FPIMAGEMAP16_H_666_
#define __FPIMAGEMAP16_H_666_


//NOTE: Not used any more!!!

#include "Map2D.h"
#ifdef OPENEXR_SUPPORT
#include <half.h>

//#include "../utils/array2d.h"
#include <vector>


/*=====================================================================
FPImageMap16
------------
Tri-component 16-bit floating point image.

//NOTE: Not used any more!!!

=====================================================================*/
class FPImageMap16 : public Map2D
{
public:
	/*=====================================================================
	FPImageMap16
	------------
	
	=====================================================================*/
	FPImageMap16(unsigned int width, unsigned int height);

	~FPImageMap16();

	// X and Y are normalised image coordinates.
	virtual const Colour3<Value> vec3SampleTiled(Coord x, Coord y) const;

	virtual Value scalarSampleTiled(Coord x, Coord y) const;


	unsigned int getWidth() const { return width; }
	unsigned int getHeight() const { return height; }

	virtual unsigned int getMapWidth() const { return width; }
	virtual unsigned int getMapHeight() const { return height; }

	std::vector<half>& getData() { return data; }

	virtual bool takesOnlyUnitIntervalValues() const { return false; }


	inline const half* getPixel(unsigned int x, unsigned int y) const;
private:

	unsigned int width, height;
	//Array2d<half> data;
	std::vector<half> data;
};


const half* FPImageMap16::getPixel(unsigned int x, unsigned int y) const
{
	return &data[(x + width * y) * 3];
}

#endif // OPENEXR_SUPPORT


#endif //__FPIMAGEMAP16_H_666_




