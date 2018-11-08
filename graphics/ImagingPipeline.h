/*=====================================================================
ImagingPipeline.h
-----------------
Copyright Glare Technologies Limited 2018 -
Generated at Wed Jul 13 13:44:31 +0100 2011
=====================================================================*/
#pragma once


#include "../indigo/RendererSettings.h"
#include "../indigo/RenderChannels.h"
#include "../utils/ArrayRef.h"
#include "Image4f.h"
#include <vector>


class PostProDiffraction;
class Image4f;
namespace Indigo { class TaskManager; }
namespace Indigo { class Task; }


namespace ImagingPipeline
{


// To avoid repeated allocations and deallocations, keep some data around in this structure, that can be passed in to successive runPipeline() calls.
struct RunPipelineScratchState
{
	RunPipelineScratchState();
	~RunPipelineScratchState();
	std::vector<Reference<Indigo::Task> > tasks; // These should only actually have type ImagePipelineTask.
	std::vector<Image4f> per_thread_tile_buffers;
	Image4f temp_summed_buffer;
	Image4f temp_AD_buffer;
};


/*
runPipeline
-----------
Runs the standard imaging pipeline on some some input image data, stored in render_channels, to generate some output image data, stored in ldr_buffer_out.

Does downsizing of the supersampled internal buffer to the output image resolution.

ldr_buffer_out should have the correct size - e.g. final width and height, or ceil(final_width / subres_factor) etc..

The input data may be in XYZ colour space (as is the case for the usual rendering), or linear sRGB space (the case for some loaded images).
XYZ_colourspace should be set accordingly.

Tonemapping can be done by setting do_tonemapping to true.

The output data will be stored as floating point data in ldr_buffer_out.

The output data will be in linear or non-linear sRGB colour space.
output_is_nonlinear will be set based on the output colour space.

The output data components will be in the range [0, 1] if do_tonemapping is true and we are processing the main beauty layers blended, or an individual beauty layer or channel.
*/
void runPipeline(
	RunPipelineScratchState& scratch_state, // Working/scratch state
	const RenderChannels& render_channels, // Input image data
	const ChannelInfo* channel, // Channel to tone-map.  if channel is NULL, then blend together all the main layers weighted with layer_weights and tone-map the blended sum.
	int final_width,
	int final_height,
	int ssf,
	const ArrayRef<RenderRegion>& render_regions,
	const ArrayRef<Vec3f>& layer_weights, // Light layer weights - used for weighting main beauty layers when blending them together.
	float image_scale, // A scale factor based on the number of samples taken and image resolution. (from PathSampler::getScale())
	float region_image_scale, // Scale factor for pixels in render regions.
	const RendererSettings& renderer_settings,
	const float* const resize_filter,
	const Reference<PostProDiffraction>& post_pro_diffraction, // May be NULL
	Image4f& ldr_buffer_out, // Output image, has alpha channel.
	bool& output_is_nonlinear, // Is ldr_buffer_out in a non-linear space?
	bool XYZ_colourspace, // Are the input layers in XYZ colour space?  If so, an XYZ -> sRGB conversion is done.
	int margin_ssf1, // Margin width (for just one side), in pixels, at ssf 1.  This may be zero for loaded LDR images. (PNGs etc..)
	Indigo::TaskManager& task_manager,
	int subres_factor = 1, // Number of times smaller resolution we will do the realtime rendering at.
	bool do_tonemapping = true // Should we actually tone-map?  Can be set to false for saving untonemapped EXRs.
);


// To avoid repeated allocations and deallocations, keep some tasks around in this structure.
struct ToNonLinearSpaceScratchState
{
	ToNonLinearSpaceScratchState();
	~ToNonLinearSpaceScratchState();
	std::vector<Reference<Indigo::Task> > tasks; // These should only actually have type ToNonLinearSpaceTask.
};


/*
Converts some tonemapped image data to non-linear sRGB space.
Does a few things in preparation for conversion to an 8-bit output image format,
such as dithering and gamma correction.
Input colour space is linear or non-linear sRGB
Output colour space is non-linear sRGB.
We also assume the output values are not premultiplied alpha.

A lock on uint8_buffer_out is expected to be held by the calling thread if uint8_buffer_out is non-null.
uint8_buffer_out will be resized to the same size as ldr_buffer_in_out.
*/
void toNonLinearSpace(
	Indigo::TaskManager& task_manager,
	ToNonLinearSpaceScratchState& scratch_state,
	bool shadow_pass,
	bool dithering,
	Image4f& ldr_buffer_in_out, // Input and output image, has alpha channel.
	bool input_is_nonlinear, // Is ldr_buffer_in_out in a non-linear colour space?
	Bitmap* uint8_buffer_out = NULL // May be NULL
);


}; // namespace ImagingPipeline
