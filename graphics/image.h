#ifndef __IMAGE_666_H__
#define __IMAGE_666_H__


#include "colour3.h"
#include "../utils/array2d.h"
#include "../utils/platform.h"
#include "../graphics/Map2D.h"
#include "assert.h"
#include <string>
#include <map>
#include "bitmap.h"
class FilterFunction;

class ImageExcep
{
public:
	ImageExcep(const std::string& s_) : s(s_) {}
	~ImageExcep(){}

	const std::string& what() const { return s; }
private:
	std::string s;
};



/*=====================================================================
Image
-----
A floating point tri-component image class.
Each component is stored as a 32-bit float.
=====================================================================*/
class Image : public Map2D
{
public:
	Image();
	Image(unsigned int width, unsigned int height);
	~Image();

	Image& operator = (const Image& other);

	typedef Colour3f ColourType;

	void setFromBitmap(const Bitmap& bmp); // will throw ImageExcep if bytespp != 3

	void copyRegionToBitmap(Bitmap& bmp_out, int x1, int y1, int x2, int y2) const; // will throw ImageExcep if bytespp != 3 && bytespp != 4

	void copyToBitmap(Bitmap& bmp_out) const;

	inline unsigned int getHeight() const { return height; }
	inline unsigned int getWidth()  const { return width; }
	inline size_t numPixels() const { return (size_t)width * (size_t)height; }

	INDIGO_STRONG_INLINE const ColourType& getPixel(unsigned int x, unsigned int y) const;
	INDIGO_STRONG_INLINE ColourType& getPixel(unsigned int x, unsigned int y);

	INDIGO_STRONG_INLINE const ColourType& getPixel(unsigned int i) const;
	INDIGO_STRONG_INLINE ColourType& getPixel(unsigned int i);

	inline const ColourType& getPixelTiled(int x, int y) const;

	//const ColourType sample(float x, float y) const;
	//const ColourType sampleTiled(float x, float y) const;

	//static Image* loadFromBitmap(const std::string& pathname);
	//throws ImageExcep
	void loadFromBitmap(const std::string& pathname);
	//throws ImageExcep
	void saveToBitmap(const std::string& pathname);

	//throws ImageExcep
	//void loadFromRAW(const std::string& pathname, int width_, int height_,
	//	float load_gain);

	inline void setPixel(unsigned int x, unsigned int y, const ColourType& colour);
	inline void incrPixel(unsigned int x, unsigned int y, const ColourType& colour);

	//throws ImageExcep
	void loadFromNFF(const std::string& pathname);
	//throws ImageExcep
	void saveAsNFF(const std::string& pathname);

	void saveTo16BitExr(const std::string& pathname) const;
	void loadFromExr(const std::string& pathname);

	void saveTo32BitExr(const std::string& pathname) const;

	//void saveToPng(const std::string& pathname, const std::map<std::string, std::string>& metadata, int border_width) const;

	void loadFromHDR(const std::string& pathname, int width, int height);

	void zero();
	void set(float s);

	void resize(unsigned int newwidth, unsigned int newheight);

	void posClamp();
	void clampInPlace(float min, float max);

	void gammaCorrect(float exponent);

	void blitToImage(Image& dest, int destx, int desty) const;
	void blitToImage(int src_start_x, int src_start_y, int src_end_x, int src_end_y, Image& dest, int destx, int desty) const;
	void addImage(const Image& other, int destx, int desty);
	void blendImage(const Image& dest, int destx, int desty);
	void subImage(const Image& dest, int destx, int desty);
	
	void overwriteImage(const Image& src, int destx, int desty);

	void scale(float factor);

	void normalise();//make the average pixel luminance == 1

	enum DOWNSIZE_FILTER
	{
		DOWNSIZE_FILTER_MN_CUBIC,
		DOWNSIZE_FILTER_BOX
	};

	void collapseSizeBoxFilter(int factor); // trims off border before collapsing
	//void collapseImage(int factor, int border_width, DOWNSIZE_FILTER filter_type, double mn_B, double mn_C);

	static void collapseImage(int factor, int border_width, const FilterFunction& filter_function, float max_component_value, const Image& in, Image& out);

	static void collapseImageNew(int factor, int border_width, int resize_filter_size, const float* const resize_filter, float max_component_value, const Image& in, Image& out);

	unsigned int getByteSize() const;

	float minLuminance() const;
	float maxLuminance() const;
	double averageLuminance() const;

	static void buildRGBFilter(const Image& original_filter, const Vec3d& filter_scales, Image& result_out);
	//void convolve(const Image& filter, Image& result_out) const;

	float minPixelComponent() const;
	float maxPixelComponent() const;

	////// Map2D interface //////////
	virtual const Colour3<Value> vec3SampleTiled(Coord x, Coord y) const;

	virtual Value scalarSampleTiled(Coord x, Coord y) const;

	virtual bool takesOnlyUnitIntervalValues() const { return false; }
	/////////////////////////////////

	static void test();

private:
	unsigned int width;
	unsigned int height;

	Array2d<ColourType> pixels;
};


const Image::ColourType& Image::getPixel(unsigned int i) const
{
	assert(i < numPixels());
	return pixels.getData()[i];
}


Image::ColourType& Image::getPixel(unsigned int i)
{
	assert(i < numPixels());
	return pixels.getData()[i];
}

const Image::ColourType& Image::getPixel(unsigned int x, unsigned int y) const
{
	assert(x >= 0 && x < width && y >= 0 && y < height);

	return pixels.elem(x, y);
}

Image::ColourType& Image::getPixel(unsigned int x, unsigned int y)
{
	assert(x >= 0 && x < width && y >= 0 && y < height);

	return pixels.elem(x, y);
}


const Image::ColourType& Image::getPixelTiled(int x, int y) const
{
	if(x < 0)
	{
		//note: could use modulo here somehow
		//while(x < 0)
		//	x += width;
		x = 0 - x;//x = -x		, so now x is positive
					//NOTE: could use bitmask here
		x = x % width;

		x = width - x - 1;

		//say x = -1;
		//x = (width - x) % width; //x = 700 - (-1) = 70;

	}
	else
	{
		x = x % width;
	}

	if(y < 0)
	{
		//note: could use modulo here somehow
		//while(y < 0)
		//	y += height;

		y = 0 - y;
					
		y = y % height;

		y = height - y - 1;

		//y = (height - y) % height;
	}
	else
	{
		y = y % height;
	}

	return getPixel(x, y);
}


void Image::setPixel(unsigned int x, unsigned int y, const ColourType& colour)
{
	assert(x >= 0 && x < width && y >= 0 && y < height);
	
	pixels.elem(x, y) = colour;
}

void Image::incrPixel(unsigned int x, unsigned int y, const ColourType& colour)
{
	assert(x >= 0 && x < width && y >= 0 && y < height);
	
	pixels.elem(x, y) += colour;
}



#endif //__IMAGE_666_H__
