/*=====================================================================
FPImageMap32.h
--------------
File created by ClassTemplate on Sun May 18 21:48:53 2008
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __FPIMAGEMAP32_H_666_
#define __FPIMAGEMAP32_H_666_

//#error: use Image.h instead

class Image;


/*=====================================================================
FPImageMap32
------------

=====================================================================*/
class FPImageMap32
{
public:
	/*=====================================================================
	FPImageMap32
	------------
	
	=====================================================================*/
	FPImageMap32();

	~FPImageMap32();


	virtual bool takesOnlyUnitIntervalValues() const { return false; }

private:
	Image* data;
};



#endif //__FPIMAGEMAP32_H_666_




