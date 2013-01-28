#include "Image4f.h"


#include "../indigo/RendererSettings.h"
#include "../utils/stringutils.h"
#include "../utils/fileutils.h"
#include "../utils/FileHandle.h"
#include "../utils/Exception.h"
#include "../maths/vec2.h"
#include "../utils/TaskManager.h"
#include "../utils/Task.h"
#include "GaussianImageFilter.h"
#include "BoxFilterFunction.h"
#include "bitmap.h"
#include <fstream>
#include <limits>
#include <cmath>
#include <cassert>
#include <stdio.h>


Image4f::Image4f()
:	pixels(0, 0)
{
}

Image4f::Image4f(size_t width_, size_t height_)
:	pixels(width_, height_)
{
}


Image4f::~Image4f()
{}


Image4f& Image4f::operator = (const Image4f& other)
{
	if(&other == this)
		return *this;

	if(getWidth() != other.getWidth() || getHeight() != other.getHeight())
	{
		pixels.resize(other.getWidth(), other.getHeight());
	}

	this->pixels = other.pixels;

	return *this;
}


void Image4f::setFromBitmap(const Bitmap& bmp, float image_gamma)
{
	if(bmp.getBytesPP() != 1 && bmp.getBytesPP() != 3 && bmp.getBytesPP() != 4)
		throw Indigo::Exception("Image bytes per pixel must be 1, 3, or 4.");

	resize(bmp.getWidth(), bmp.getHeight());

	if(bmp.getBytesPP() == 1)
	{
		const float factor = 1.0f / 255.0f;
		for(size_t y = 0; y < bmp.getHeight(); ++y)
		for(size_t x = 0; x < bmp.getWidth();  ++x)
		{
			float v = std::pow((float)*bmp.getPixel(x, y) * factor, image_gamma);
			setPixel(x, y, Colour4f(
				v, v, v, 1.0f
			));
		}
	}
	else if(bmp.getBytesPP() == 3)
	{
		assert(bmp.getBytesPP() == 3);

		const float factor = 1.0f / 255.0f;
		for(size_t y = 0; y < bmp.getHeight(); ++y)
		for(size_t x = 0; x < bmp.getWidth();  ++x)
		{
			setPixel(x, y,
				Colour4f(
					std::pow((float)bmp.getPixel(x, y)[0] * factor, image_gamma),
					std::pow((float)bmp.getPixel(x, y)[1] * factor, image_gamma),
					std::pow((float)bmp.getPixel(x, y)[2] * factor, image_gamma),
					1.0f
					)
				);
		}
	}
	else
	{
		assert(bmp.getBytesPP() == 4);

		const float factor = 1.0f / 255.0f;
		for(size_t y = 0; y < bmp.getHeight(); ++y)
		for(size_t x = 0; x < bmp.getWidth();  ++x)
		{
			setPixel(x, y,
				Colour4f(
					std::pow((float)bmp.getPixel(x, y)[0] * factor, image_gamma),
					std::pow((float)bmp.getPixel(x, y)[1] * factor, image_gamma),
					std::pow((float)bmp.getPixel(x, y)[2] * factor, image_gamma),
					std::pow((float)bmp.getPixel(x, y)[3] * factor, image_gamma)
					)
				);
		}
	}
}


// Will throw ImageExcep if bytespp != 3 && bytespp != 4
void Image4f::copyRegionToBitmap(Bitmap& bmp_out, int x1, int y1, int x2, int y2) const
{
	if(bmp_out.getBytesPP() != 3 && bmp_out.getBytesPP() != 4)
		throw Indigo::Exception("BytesPP != 3 or 4");

	if(x1 < 0 || y1 < 0 || x1 >= x2 || y1 >= y2 || x2 > (int)getWidth() || y2 > (int)getHeight())
		throw Indigo::Exception("Region coordinates are invalid");

	const int out_width = x2 - x1;
	const int out_height = y2 - y1;

	bmp_out.resize(out_width, out_height, bmp_out.getBytesPP());

	for(int y = y1; y < y2; ++y)
	for(int x = x1; x < x2; ++x)
	{
		unsigned char* pixel = bmp_out.getPixelNonConst(x - x1, y - y1);
		pixel[0] = (unsigned char)(getPixel(x, y).x[0] * 255.0f);
		pixel[1] = (unsigned char)(getPixel(x, y).x[1] * 255.0f);
		pixel[2] = (unsigned char)(getPixel(x, y).x[2] * 255.0f);
		if(bmp_out.getBytesPP() == 4)
			pixel[3] = (unsigned char)(getPixel(x, y).x[3] * 255.0f);
	}
}


void Image4f::copyToBitmap(Bitmap& bmp_out) const
{
	bmp_out.resize(getWidth(), getHeight(), 4);

	for(size_t y = 0; y < getHeight(); ++y)
	for(size_t x = 0; x < getWidth();  ++x)
	{
		const ColourType& p = getPixel(x, y);

		unsigned char* pixel = bmp_out.getPixelNonConst(x, y);
		pixel[0] = (unsigned char)(p.x[0] * 255.0f);
		pixel[1] = (unsigned char)(p.x[1] * 255.0f);
		pixel[2] = (unsigned char)(p.x[2] * 255.0f);
		pixel[3] = (unsigned char)(p.x[3] * 255.0f);
	}
}


void Image4f::zero()
{
	set(0);
}


void Image4f::set(const float s)
{
	const ColourType s_colour(s);

	const size_t num = numPixels();
	for(size_t i = 0; i < num; ++i)
		getPixel(i) = s_colour;
}


void Image4f::resize(size_t newwidth, size_t newheight)
{
	if(getWidth() == newwidth && getHeight() == newheight)
		return;

	pixels.resize(newwidth, newheight);
}


//void Image4f::posClamp()
//{
//	const size_t num = numPixels();
//	for(size_t i = 0; i < num; ++i)
//		getPixel(i).positiveClipComponents();
//}
//
//
//void Image4f::clampInPlace(float min, float max)
//{
//	const size_t num = numPixels();
//	for(size_t i = 0; i < num; ++i)
//		getPixel(i).clampInPlace(min, max);
//}

//
//void Image4f::gammaCorrect(float exponent)
//{
//	const size_t num = numPixels();
//	for(size_t i = 0; i < num; ++i)
//	{
//		const ColourType colour = getPixel(i);
//		ColourType newcolour(
//			std::pow(colour.r, exponent),
//			std::pow(colour.g, exponent),
//			std::pow(colour.b, exponent)
//		);
//
//		getPixel(i) = newcolour;
//	}
//}
//
//
//void Image4f::scale(float factor)
//{
//	const size_t num = numPixels();
//	for(size_t i = 0; i < num; ++i)
//		getPixel(i) *= factor;
//}


void Image4f::blitToImage(Image4f& dest, int destx, int desty) const
{
	const int s_h = (int)getHeight();
	const int s_w = (int)getWidth();
	const int d_h = (int)dest.getHeight();
	const int d_w = (int)dest.getWidth();

	for(int y = 0; y < s_h; ++y)
	for(int x = 0; x < s_w;  ++x)
	{
		const int dx = x + destx;
		const int dy = y + desty;

		if(dx >= 0 && dx < d_w && dy >= 0 && dy < d_h)
			dest.setPixel(dx, dy, getPixel(x, y));
	}
}


void Image4f::blitToImage(int src_start_x, int src_start_y, int src_end_x, int src_end_y, Image4f& dest, int destx, int desty) const
{
	const int use_src_start_x = myMax(0, src_start_x);
	const int use_src_start_y = myMax(0, src_start_y);

	src_end_x = myMin(src_end_x, (int)getWidth());
	src_end_y = myMin(src_end_y, (int)getHeight());

	const int d_h = (int)dest.getHeight();
	const int d_w = (int)dest.getWidth();

	for(int y = use_src_start_y; y < src_end_y; ++y)
	for(int x = use_src_start_x; x < src_end_x; ++x)
	{
		const int dx = (x - src_start_x) + destx;
		const int dy = (y - src_start_y) + desty;

		if(dx >= 0 && dx < d_w && dy >= 0 && dy < d_h)
			dest.setPixel(dx, dy, getPixel(x, y));
	}
}


void Image4f::addImage(const Image4f& img, const int destx, const int desty, const float alpha)
{
	const int h = (int)getHeight();
	const int w = (int)getWidth();

	for(int y = 0; y < h; ++y)
	for(int x = 0; x < w;  ++x)
	{
		const int dx = x + destx;
		const int dy = y + desty;

		if(dx >= 0 && dx < w && dy >= 0 && dy < h)
			getPixel(dx, dy) += img.getPixel(x, y) * alpha;
	}
}


void Image4f::blendImage(const Image4f& img, const int destx, const int desty, const Colour4f& colour)
{
	const int h = (int)getHeight();
	const int w = (int)getWidth();

	for(int y = 0; y < (int)img.getHeight(); ++y)
	for(int x = 0; x < (int)img.getWidth();  ++x)
	{
		const int dx = x + destx;
		const int dy = y + desty;

		if(dx >= 0 && dx < w && dy >= 0 && dy < h)
		{
			// setPixel(dx, dy, solid_colour * img.getPixel(x, y).r * alpha + getPixel(dx, dy) * (1 - img.getPixel(x, y).r * alpha));

			float use_alpha = img.getPixel(x, y).x[0] * colour.x[3];

			const Colour4f new_col = colour * use_alpha + getPixel(dx, dy) * (1 - use_alpha);
			setPixel(dx, dy, new_col);
		}
	}
}


/*

void Image4f::subImage(const Image4f& img, int destx, int desty)
{
	const int h = (int)getHeight();
	const int w = (int)getWidth();

	for(int y = 0; y < (int)img.getHeight(); ++y)
	for(int x = 0; x < (int)img.getWidth();  ++x)
	{
		int dx = x + destx;
		int dy = y + desty;

		if(dx >= 0 && dx < w && dy >= 0 && dy < h)
			getPixel(dx, dy).addMult(img.getPixel(x, y), -1.0f); //NOTE: slow :)
	}
}


void Image4f::overwriteImage(const Image4f& img, int destx, int desty)
{
	const int h = (int)getHeight();
	const int w = (int)getWidth();

	for(int y = 0; y < (int)img.getHeight(); ++y)
	for(int x = 0; x < (int)img.getWidth();  ++x)
	{
		const int dx = x + destx;
		const int dy = y + desty;
		if(dx >= 0 && dx < w && dy >= 0 && dy < h)
			setPixel(dx, dy, img.getPixel(x, y));
	}
}


// Make the average pixel luminance == 1
void Image4f::normalise()
{
	if(getHeight() == 0 || getWidth() == 0)
		return;

	double av_lum = 0;
	for(size_t i = 0; i < numPixels(); ++i)
		av_lum += (double)getPixel(i).luminance();
	av_lum /= (double)(numPixels());

	const float factor = (float)(1 / av_lum);
	for(size_t i = 0; i < numPixels(); ++i)
		getPixel(i) *= factor;
}*/



// trims off border before collapsing
void Image4f::collapseSizeBoxFilter(int factor)
{
	Image4f out;
	BoxFilterFunction ff;
	collapseImage(
		factor,
		0, // border
		ff,
		1.0f, // max component value
		*this,
		out
		);

	*this = out;
}


void Image4f::collapseImage(int factor, int border_width, const FilterFunction& filter_function, float max_component_value, const Image4f& in, Image4f& out)
{
	assert(border_width >= 0);
	assert((int)in.getWidth()  > border_width * 2);
	assert((int)in.getHeight() > border_width * 2);
	assert((in.getWidth() - (border_width * 2)) % factor == 0);

	//Image4f out((width - (border_width * 2)) / factor, (height - (border_width * 2)) / factor);
	out.resize((in.getWidth() - (border_width * 2)) / factor, (in.getHeight() - (border_width * 2)) / factor);

	const double radius_src = filter_function.supportRadius() * (double)factor;
	const int filter_width = (int)ceil(radius_src * 2.0); // neg_rad_src + pos_rad_src;

	//double sum = 0.0;
	Array2d<float> filter(filter_width, filter_width);
	for(int y = 0; y < filter_width; ++y)
	{
		const double pos_y = (double)y + 0.5; // y coordinate in src pixels
		const double abs_dy_src = fabs(pos_y - radius_src);

		for(int x = 0; x < filter_width; ++x)
		{
			const double pos_x = (double)x + 0.5; // x coordinate in src pixels
			const double abs_dx_src = fabs(pos_x - radius_src);

			filter.elem(x, y) = (float)(filter_function.eval(abs_dx_src / (double)factor) *
										filter_function.eval(abs_dy_src / (double)factor) / (double)(factor * factor));
			//sum += (double)filter.elem(x, y);
		}
	}

	//#ifndef INDIGO_NO_OPENMP
	//#pragma omp parallel for
	//#endif
	for(int y=0; y<(int)out.getHeight(); ++y)
	{
		// Get the y-range of pixels in the source Image4f that lie in the filter support for the destination pixel.
		const int support_y = (y * factor) + (int)floor((0.5 - filter_function.supportRadius()) * (double)factor) + border_width;
		const int src_y_min = myMax(0, support_y);
		const int src_y_max = myMin((int)in.getHeight(), support_y + filter_width);

		int support_x = (int)floor((0.5 - filter_function.supportRadius()) * (double)factor) + border_width;

		for(int x=0; x<(int)out.getWidth(); ++x)
		{
			// Get the x-range of pixels in the source Image4f that lie in the filter support for the destination pixel.
			const int src_x_min = myMax(0, support_x);
			const int src_x_max = myMin((int)in.getWidth(), support_x + filter_width);

			Colour4f c(0.0f); // Running sum of source pixel value * filter value.

			// For each pixel in filter support of source Image4f
			for(int sy=src_y_min; sy<src_y_max; ++sy)
			{
				const int filter_y = sy - support_y; //(sy - src_y_center) + neg_rad;
				assert(filter_y >= 0 && filter_y < filter_width);

				for(int sx=src_x_min; sx<src_x_max; ++sx)
				{
					const int filter_x = sx - support_x; //(sx - src_x_center) + neg_rad;
					assert(filter_x >= 0 && filter_x < filter_width);

					assert(in.getPixel(sx, sy).r >= 0.0 && in.getPixel(sx, sy).g >= 0.0 && in.getPixel(sx, sy).b >= 0.0);
					assert(isFinite(in.getPixel(sx, sy).r) && isFinite(in.getPixel(sx, sy).g) && isFinite(in.getPixel(sx, sy).b));

					c.addMult(in.getPixel(sx, sy), filter.elem(filter_x, filter_y));
				}
			}

			//assert(c.r >= 0.0 && c.g >= 0.0 && c.b >= 0.0);
			assert(isFinite(c.r) && isFinite(c.g) && isFinite(c.b));

			c.clampInPlace(0.0f, max_component_value); // Make sure components can't go below zero or above max_component_value
			out.setPixel(x, y, c);

			support_x += factor;
		}
	}
}


struct DownsampleImageTaskClosure
{
	Image4f::ColourType const * in_buffer;
	Image4f::ColourType		 * out_buffer;
	const float * resize_filter;
	ptrdiff_t factor, border_width, in_xres, in_yres, filter_bound, out_xres, out_yres;
	float pre_clamp;
};


class DownsampleImageTask : public Indigo::Task
{
public:
	DownsampleImageTask(const DownsampleImageTaskClosure& closure_, size_t begin_, size_t end_) : closure(closure_), begin((int)begin_), end((int)end_) {}

	virtual void run(size_t thread_index)
	{
		// Copy to local variables for performance reasons.
		Image4f::ColourType const * const in_buffer  = closure.in_buffer;
		Image4f::ColourType		* const out_buffer = closure.out_buffer;
		const float * const resize_filter = closure.resize_filter;
		const ptrdiff_t factor = closure.factor;
		const ptrdiff_t border_width = closure.border_width;
		const ptrdiff_t in_xres = closure.in_xres;
		const ptrdiff_t in_yres = closure.in_yres;
		const ptrdiff_t filter_bound = closure.filter_bound;
		const ptrdiff_t out_xres = closure.out_xres;
		const ptrdiff_t out_yres = closure.out_yres;
		const float pre_clamp = closure.pre_clamp;

		for(int y = begin; y < end; ++y)
		for(int x = 0; x < out_xres; ++x)
		{
			const ptrdiff_t u_min = (x + border_width) * factor + factor / 2 - filter_bound; assert(u_min >= 0);
			const ptrdiff_t v_min = (y + border_width) * factor + factor / 2 - filter_bound; assert(v_min >= 0);
			const ptrdiff_t u_max = (x + border_width) * factor + factor / 2 + filter_bound; assert(u_max < in_xres);
			const ptrdiff_t v_max = (y + border_width) * factor + factor / 2 + filter_bound; assert(v_max < in_yres);

			Image4f::ColourType weighted_sum(0);
			uint32 filter_addr = 0;
			for(ptrdiff_t v = v_min; v <= v_max; ++v)
			for(ptrdiff_t u = u_min; u <= u_max; ++u)
			{
				const ptrdiff_t addr = v * in_xres + u;
				assert(addr >= 0 && addr < (ptrdiff_t)(in_xres * in_yres)/*img_in.numPixels()*/);

				weighted_sum.addMult(in_buffer[addr], resize_filter[filter_addr++]);
			}

			assert(isFinite(weighted_sum.x[0]) && isFinite(weighted_sum.x[1]) && isFinite(weighted_sum.x[2]));

			weighted_sum.clampInPlace(0.0f, pre_clamp); // Make sure components can't go below zero or above pre_clamp
			out_buffer[y * out_xres + x] = weighted_sum;
		}
	}

	const DownsampleImageTaskClosure& closure;
	int begin, end;
};


void Image4f::downsampleImage(const ptrdiff_t factor, const ptrdiff_t border_width,
							const ptrdiff_t filter_span, const float * const resize_filter, const float pre_clamp,
							const Image4f& img_in, Image4f& img_out, Indigo::TaskManager& task_manager)
{
	assert(border_width >= 0);						// have padding pixels
	assert((int)img_in.getWidth()  > border_width * 2);	// have at least one interior pixel in x
	assert((int)img_in.getHeight() > border_width * 2);	// have at least one interior pixel in y
	assert(img_in.getWidth()  % factor == 0);		// padded Image4f is multiple of supersampling factor
	assert(img_in.getHeight() % factor == 0);		// padded Image4f is multiple of supersampling factor

	assert(filter_span > 0);
	assert(resize_filter != 0);

	const ptrdiff_t in_xres  = (ptrdiff_t)img_in.getWidth();
	const ptrdiff_t in_yres  = (ptrdiff_t)img_in.getHeight();
	const ptrdiff_t filter_bound = filter_span / 2 - 1;

	const ptrdiff_t out_xres = (ptrdiff_t)RendererSettings::computeFinalWidth((int)img_in.getWidth(), (int)factor);
	const ptrdiff_t out_yres = (ptrdiff_t)RendererSettings::computeFinalHeight((int)img_in.getHeight(), (int)factor);
	img_out.resize((size_t)out_xres, (size_t)out_yres);

	ColourType const * const in_buffer  = &img_in.getPixel(0, 0);
	ColourType		 * const out_buffer = &img_out.getPixel(0, 0);

	/*#ifndef INDIGO_NO_OPENMP
	#pragma omp parallel for
	#endif
	for(int y = 0; y < out_yres; ++y)
	for(int x = 0; x < out_xres; ++x)
	{
		const ptrdiff_t u_min = (x + border_width) * factor + factor / 2 - filter_bound; assert(u_min >= 0);
		const ptrdiff_t v_min = (y + border_width) * factor + factor / 2 - filter_bound; assert(v_min >= 0);
		const ptrdiff_t u_max = (x + border_width) * factor + factor / 2 + filter_bound; assert(u_max < (ptrdiff_t)img_in.getWidth());
		const ptrdiff_t v_max = (y + border_width) * factor + factor / 2 + filter_bound; assert(v_max < (ptrdiff_t)img_in.getHeight());

		ColourType weighted_sum(0);
		uint32 filter_addr = 0;
		for(ptrdiff_t v = v_min; v <= v_max; ++v)
		for(ptrdiff_t u = u_min; u <= u_max; ++u)
		{
			const ptrdiff_t addr = v * in_xres + u;
			assert(addr >= 0 && addr < (ptrdiff_t)img_in.numPixels());

			weighted_sum.addMult(in_buffer[addr], resize_filter[filter_addr++]);
		}

		assert(isFinite(weighted_sum.r) && isFinite(weighted_sum.g) && isFinite(weighted_sum.b));

		weighted_sum.clampInPlace(0.0f, pre_clamp); // Make sure components can't go below zero or above pre_clamp
		out_buffer[y * out_xres + x] = weighted_sum;
	}*/

	DownsampleImageTaskClosure closure;
	closure.in_buffer = in_buffer;
	closure.out_buffer = out_buffer;
	closure.resize_filter = resize_filter;
	closure.factor = factor;
	closure.border_width = border_width;
	closure.in_xres = in_xres;
	closure.in_yres = in_yres;
	closure.filter_bound = filter_bound;
	closure.out_xres = out_xres;
	closure.out_yres = out_yres;
	closure.pre_clamp = pre_clamp;

	// Blur in x direction
	task_manager.runParallelForTasks<DownsampleImageTask, DownsampleImageTaskClosure>(closure, 0, out_yres);
}


size_t Image4f::getByteSize() const
{
	return numPixels() * 3 * sizeof(float);
}
/*

float Image4f::minLuminance() const
{
	float minlum = std::numeric_limits<float>::max();
	for(size_t i = 0; i < numPixels(); ++i)
		minlum = myMin(minlum, getPixel(i).luminance());
	return minlum;
}


float Image4f::maxLuminance() const
{
	float maxlum = -std::numeric_limits<float>::max();
	for(size_t i = 0; i < numPixels(); ++i)
		maxlum = myMax(maxlum, getPixel(i).luminance());
	return maxlum;
}
*/

double Image4f::averageLuminance() const
{
	double sum = 0;
	for(size_t i = 0; i < numPixels(); ++i)
		sum += (double)getPixel(i).x[1]; // TEMP HACK luminance();
	return sum / (double)numPixels();
}


float Image4f::minPixelComponent() const
{
	Vec4f v(std::numeric_limits<float>::max());
	for(size_t i = 0; i < numPixels(); ++i)
		v = min(v, getPixel(i).v);

	return myMin(myMin(v.x[0], v.x[1]), myMin(v.x[2], v.x[3]));


	/*float x = std::numeric_limits<float>::max();
	for(size_t i = 0; i < numPixels(); ++i)
		x = myMin(x, myMin(getPixel(i).r, myMin(getPixel(i).g, getPixel(i).b)));
	return x;*/
}


float Image4f::maxPixelComponent() const
{
	Vec4f v(-std::numeric_limits<float>::max());
	for(size_t i = 0; i < numPixels(); ++i)
		v = max(v, getPixel(i).v);

	return myMax(myMax(v.x[0], v.x[1]), myMax(v.x[2], v.x[3]));

	/*
	return myMin(myMin(v.x[0], v.x[1]), myMin(v.x[2], v.x[3]));
	float x = -std::numeric_limits<float>::max();
	for(size_t i = 0; i < numPixels(); ++i)
		x = myMax(x, myMax(getPixel(i).r, myMax(getPixel(i).g, getPixel(i).b)));
	return x;*/
}



/*
const Colour3<Image4f::Value> Image4f::vec3SampleTiled(Coord u, Coord v) const
{
	//return sampleTiled((float)x, (float)y).toColour3d();

	Colour3<Value> colour_out;

	Coord intpart; // not used
	Coord u_frac_part = std::modf(u, &intpart);
	Coord v_frac_part = std::modf(1 - v, &intpart); // 1.0 - v because we want v=0 to be at top of Image4f, and v=1 to be at bottom.

	if(u_frac_part < 0)
		u_frac_part = 1 + u_frac_part;
	if(v_frac_part < 0)
		v_frac_part = 1 + v_frac_part;
	assert(Maths::inHalfClosedInterval<Coord>(u_frac_part, 0.0, 1.0));
	assert(Maths::inHalfClosedInterval<Coord>(v_frac_part, 0.0, 1.0));

	// Convert from normalised Image4f coords to pixel coordinates
	const Coord u_pixels = u_frac_part * (Coord)getWidth();
	const Coord v_pixels = v_frac_part * (Coord)getHeight();
	assert(Maths::inHalfClosedInterval<Coord>(u_pixels, 0.0, (Coord)getWidth()));
	assert(Maths::inHalfClosedInterval<Coord>(v_pixels, 0.0, (Coord)getHeight()));

	const unsigned int ut = (unsigned int)u_pixels;
	const unsigned int vt = (unsigned int)v_pixels;
	assert(ut >= 0 && ut < getWidth());
	assert(vt >= 0 && vt < getHeight());

	const unsigned int ut_1 = (ut + 1) % getWidth();
	const unsigned int vt_1 = (vt + 1) % getHeight();

	const Coord ufrac = u_pixels - (Coord)ut;
	const Coord vfrac = v_pixels - (Coord)vt;
	const Coord oneufrac = 1 - ufrac;
	const Coord onevfrac = 1 - vfrac;

	// Top left pixel
	{
		const float* pixel = getPixel(ut, vt).data();
		const Value factor = oneufrac * onevfrac;
		colour_out.r = pixel[0] * factor;
		colour_out.g = pixel[1] * factor;
		colour_out.b = pixel[2] * factor;
	}

	// Top right pixel
	{
		const float* pixel = getPixel(ut_1, vt).data();
		const Value factor = ufrac * onevfrac;
		colour_out.r += pixel[0] * factor;
		colour_out.g += pixel[1] * factor;
		colour_out.b += pixel[2] * factor;
	}

	// Bottom left pixel
	{
		const float* pixel = getPixel(ut, vt_1).data();
		const Value factor = oneufrac * vfrac;
		colour_out.r += pixel[0] * factor;
		colour_out.g += pixel[1] * factor;
		colour_out.b += pixel[2] * factor;
	}

	// Bottom right pixel
	{
		const float* pixel = getPixel(ut_1, vt_1).data();
		const Value factor = ufrac * vfrac;
		colour_out.r += pixel[0] * factor;
		colour_out.g += pixel[1] * factor;
		colour_out.b += pixel[2] * factor;
	}

	return colour_out;
}*/


//Image4f::Value Image4f::scalarSampleTiled(Coord x, Coord y) const
//{
//	const Colour3<Value> col = vec3SampleTiled(x, y);
//	return (col.r + col.g + col.b) * static_cast<Image4f::Value>(1.0 / 3.0);
//}
//
//
//Reference<Image4f> Image4f::convertToImage() const
//{
//	// Return copy of this Image4f.
//	return Reference<Image4f>(new Image4f(*this));
//}

//
//Reference<Map2D> Image4f::getBlurredLinearGreyScaleImage(Indigo::TaskManager& task_manager) const
//{
//	// Blur the Image4f
//	Image4f blurred_img(getWidth(), getHeight());
//	GaussianImageFilter::gaussianFilter(
//		*this, 
//		blurred_img, 
//		(float)myMax(getWidth(), getHeight()) * 0.01f, // standard dev in pixels
//		task_manager
//		);
//
//	return Reference<Map2D>(new Image4f(blurred_img));
//}
//
//
//Reference<Map2D> Image4f::resizeToImage(const int target, bool& is_linear) const
//{
//	// Image4f class always loads fp data, so should always be in linear space
//	is_linear = true;
//
//	size_t tex_xres, tex_yres;
//	
//	if(this->getHeight() > this->getWidth())
//	{
//		tex_xres = (size_t)((float)this->getWidth() * (float)target / (float)this->getHeight());
//		tex_yres = (size_t)target;
//	}
//	else
//	{
//		tex_xres = (size_t)target;
//		tex_yres = (size_t)((float)this->getHeight() * (float)target / (float)this->getWidth());
//	}
//
//	const float inv_tex_xres = 1.0f / tex_xres;
//	const float inv_tex_yres = 1.0f / tex_yres;
//
//	Image4f* Image4f = new Image4f(tex_xres, tex_yres);
//	Reference<Map2D> map_2d = Reference<Map2D>(Image4f);
//
//	for(size_t y = 0; y < tex_yres; ++y)
//	for(size_t x = 0; x < tex_xres; ++x)
//	{
//		const ColourType texel = this->vec3SampleTiled(x * inv_tex_xres, (tex_yres - y - 1) * inv_tex_yres);
//
//		Image4f->setPixel(x, y, texel);
//	}
//
//	return map_2d;
//}


//unsigned int Image4f::getBytesPerPixel() const
//{
//	return sizeof(ColourType);
//}


#if BUILD_TESTS


#include "../indigo/TestUtils.h"
#include "../indigo/RendererSettings.h"
#include "../graphics/MitchellNetravaliFilterFunction.h"




#endif // BUILD_TESTS
