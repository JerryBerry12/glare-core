#pragma once


#include "colour3.h"
#include "../utils/array2d.h"
#include "../utils/platform.h"
#include "../graphics/Map2D.h"
#include "assert.h"
#include <string>
class Bitmap;
class FilterFunction;
class OutStream;
class InStream;

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
	Image(size_t width, size_t height);
	~Image();

	Image& operator = (const Image& other);

	typedef Colour3f ColourType;

	void setFromBitmap(const Bitmap& bmp, float image_gamma); // will throw ImageExcep if bytespp != 3

	void copyRegionToBitmap(Bitmap& bmp_out, int x1, int y1, int x2, int y2) const; // will throw ImageExcep if bytespp != 3 && bytespp != 4

	void copyToBitmap(Bitmap& bmp_out) const;

	inline size_t getHeight() const { return pixels.getHeight(); }
	inline size_t getWidth()  const { return pixels.getWidth(); }
	inline size_t numPixels() const { return pixels.getWidth() * pixels.getHeight(); }

	INDIGO_STRONG_INLINE const ColourType& getPixel(size_t x, size_t y) const;
	INDIGO_STRONG_INLINE ColourType& getPixel(size_t x, size_t y);

	INDIGO_STRONG_INLINE const ColourType& getPixel(size_t i) const;
	INDIGO_STRONG_INLINE ColourType& getPixel(size_t i);

	inline const ColourType& getPixelTiled(int x, int y) const;

	inline void setPixel(size_t x, size_t y, const ColourType& colour);
	inline void incrPixel(size_t x, size_t y, const ColourType& colour);

	void loadFromHDR(const std::string& pathname, int width, int height);

	void zero();
	void set(const ColourType& c);

	void resize(size_t newwidth, size_t newheight);

	void posClamp();
	void clampInPlace(float min, float max);

	void gammaCorrect(float exponent);

	void blitToImage(Image& dest, int destx, int desty) const;
	void blitToImage(int src_start_x, int src_start_y, int src_end_x, int src_end_y, Image& dest, int destx, int desty) const;
	void addImage(const Image& other, int destx, int desty, float alpha = 1);
	void addImage(const Image& other); // throws ImageExcep if dimensions not the same
	void subImage(const Image& dest, int destx, int desty);
	void blendImage(const Image& dest, int destx, int desty, const Colour3f& solid_colour, float alpha = 1);
	
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

	//static void downsampleImage(const ptrdiff_t factor, const ptrdiff_t border_width,
	//							const ptrdiff_t filter_span, const float * const resize_filter, const float pre_clamp,
	//							const Image& img_in, Image& img_out, Indigo::TaskManager& task_manager);

	size_t getByteSize() const;

	float minLuminance() const;
	float maxLuminance() const;
	double averageLuminance() const;
	double averageValue() const;

	//static void buildRGBFilter(const Image& original_filter, const Vec3d& filter_scales, Image& result_out);
	//void convolve(const Image& filter, Image& result_out) const;

	float minPixelComponent() const;
	float maxPixelComponent() const;

	////// Map2D interface //////////
	virtual unsigned int getMapWidth() const { return (unsigned int)getWidth(); }
	virtual unsigned int getMapHeight() const { return (unsigned int)getHeight(); }

	virtual const Colour3<Value> pixelColour(size_t x, size_t y) const { return pixels.elem(x, y); }

	virtual const Colour3<Value> vec3SampleTiled(Coord x, Coord y) const;

	virtual Value scalarSampleTiled(Coord x, Coord y) const;

	virtual bool takesOnlyUnitIntervalValues() const { return false; }

	virtual Reference<Image> convertToImage() const;

	virtual Reference<Map2D> getBlurredLinearGreyScaleImage(Indigo::TaskManager& task_manager) const;

	virtual Reference<Map2D> resizeToImage(const int width, bool& is_linear) const;

	virtual unsigned int getBytesPerPixel() const;
	/////////////////////////////////

	static void test();

private:
	//unsigned int width;
	//unsigned int height;

	Array2d<ColourType> pixels;
};


void writeToStream(const Image& im, OutStream& stream);
void readFromStream(InStream& stream, Image& image);


const Image::ColourType& Image::getPixel(size_t i) const
{
	assert(i < numPixels());
	return pixels.getData()[i];
}


Image::ColourType& Image::getPixel(size_t i)
{
	assert(i < numPixels());
	return pixels.getData()[i];
}


const Image::ColourType& Image::getPixel(size_t x, size_t y) const
{
	assert(x < pixels.getWidth() && y < pixels.getHeight());

	return pixels.elem(x, y);
}


Image::ColourType& Image::getPixel(size_t x, size_t y)
{
	assert(x < pixels.getWidth() && y < pixels.getHeight());

	return pixels.elem(x, y);
}


const Image::ColourType& Image::getPixelTiled(int x, int y) const
{
	const int w = (int)getWidth();
	const int h = (int)getHeight();

	if(x < 0)
	{
		//note: could use modulo here somehow
		//while(x < 0)
		//	x += width;
		x = 0 - x;//x = -x		, so now x is positive
					//NOTE: could use bitmask here
		x = x % w;

		x = w - x - 1;

		//say x = -1;
		//x = (width - x) % width; //x = 700 - (-1) = 70;
	}
	else
	{
		x = x % w;
	}

	if(y < 0)
	{
		//note: could use modulo here somehow
		//while(y < 0)
		//	y += height;

		y = 0 - y;
					
		y = y % h;

		y = h - y - 1;

		//y = (height - y) % height;
	}
	else
	{
		y = y % h;
	}

	return getPixel(x, y);
}


void Image::setPixel(size_t x, size_t y, const ColourType& colour)
{
	pixels.elem(x, y) = colour;
}

void Image::incrPixel(size_t x, size_t y, const ColourType& colour)
{
	pixels.elem(x, y) += colour;
}
