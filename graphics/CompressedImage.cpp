/*=====================================================================
CompressedImage.cpp
-------------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#include "CompressedImage.h"


#include "ImageMap.h"
#include "../utils/Exception.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/StringUtils.h"


CompressedImage::CompressedImage()
:	width(0), height(0), N(0), gamma(2.2f)
{
}


CompressedImage::CompressedImage(size_t width_, size_t height_, size_t N_)
:	width(width_), height(height_), N(N_), gamma(2.2f), ds_over_2(0.5f / width_), dt_over_2(0.5f / height_)
{
	assert(N == 3 || N == 4);
}


CompressedImage::~CompressedImage() {}


const Colour4f CompressedImage::pixelColour(size_t x, size_t y) const
{
	assert(0);
	return Colour4f(0.f);
}


// X and Y are normalised image coordinates.
// (X, Y) = (0, 0) is at the bottom left of the image.
// Although this returns an SSE 4-vector, only the first three RGB components will be set.
const Colour4f CompressedImage::vec3SampleTiled(Coord u, Coord v) const
{
	assert(0);
	return Colour4f(0.f);
}


static inline float scaleValue(float x) { return x * (1 / (Map2D::Value)255); } // Similar to UInt8ComponentValueTraits::scaleValue in ImageMap.h


// Used by TextureDisplaceMatParameter<>::eval(), for displacement and blend factor evaluation (channel 0) and alpha evaluation (channel N-1)
Map2D::Value CompressedImage::sampleSingleChannelTiled(Coord u, Coord v, size_t channel) const
{
	assert(0);
	return 0.f;
}


// s and t are normalised image coordinates.
// Returns texture value (v) at (s, t)
// Also returns dv/ds and dv/dt.
Map2D::Value CompressedImage::getDerivs(Coord s, Coord t, Value& dv_ds_out, Value& dv_dt_out) const
{
	dv_ds_out = dv_dt_out = 0;
	assert(0);
	return 0.f;
}


Reference<Map2D> CompressedImage::extractAlphaChannel() const
{
	assert(0);
	return Reference<Map2D>();
}


bool CompressedImage::isAlphaChannelAllWhite() const
{
	return true;
}


Reference<Map2D> CompressedImage::extractChannelZero() const
{
	assert(0);
	return Reference<Map2D>();
}


Reference<ImageMapFloat> CompressedImage::extractChannelZeroLinear() const
{
	assert(0);
	return Reference<ImageMapFloat>();
}


Reference<Image> CompressedImage::convertToImage() const
{
	assert(0);
	return Reference<Image>();
}


Reference<Map2D> CompressedImage::getBlurredLinearGreyScaleImage(Indigo::TaskManager& task_manager) const
{
	assert(0);
	return Reference<Map2D>();
}


Reference<ImageMap<float, FloatComponentValueTraits> > CompressedImage::resizeToImageMapFloat(const int target_width, bool& is_linear_out) const
{
	assert(0);
	return Reference<ImageMapFloat>();
}


Reference<Map2D> CompressedImage::resizeMidQuality(const int /*new_width*/, const int /*new_height*/, Indigo::TaskManager& /*task_manager*/) const
{ 
	assert(0); 
	return Reference<Map2D>();
}


size_t CompressedImage::getBytesPerPixel() const
{
	return N; // NOTE: this gives the uncompressed size.
}


size_t CompressedImage::getByteSize() const
{
	assert(0);
	return 1; // TEMP  data.dataSizeBytes();
}


#if BUILD_TESTS


#include "../indigo/globals.h"
#include "../indigo/TestUtils.h"


void CompressedImage::test()
{
	conPrint("CompressedImage::test()");

}


#endif // BUILD_TESTS